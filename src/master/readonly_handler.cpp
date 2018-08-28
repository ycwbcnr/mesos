// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#include "master/master.hpp"

#include <vector>
#include <string>

#include <mesos/mesos.hpp>

#include <mesos/authorizer/authorizer.hpp>

#include <process/owned.hpp>
#include <process/http.hpp>

#include <stout/foreach.hpp>
#include <stout/hashmap.hpp>
#include <stout/hashset.hpp>
#include <stout/jsonify.hpp>
#include <stout/option.hpp>
#include <stout/representation.hpp>

#include "common/build.hpp"
#include "common/http.hpp"


using process::Owned;

using process::http::OK;

using mesos::authorization::VIEW_EXECUTOR;
using mesos::authorization::VIEW_FLAGS;
using mesos::authorization::VIEW_FRAMEWORK;
using mesos::authorization::VIEW_ROLE;
using mesos::authorization::VIEW_TASK;

using std::vector;
using std::string;

namespace mesos {
namespace internal {
namespace master {

// The summary representation of `T` to support the `/state-summary` endpoint.
// e.g., `Summary<Slave>`.
template <typename T>
struct Summary : Representation<T>
{
  using Representation<T>::Representation;
};


// The full representation of `T` to support the `/state` endpoint.
// e.g., `Full<Slave>`.
template <typename T>
struct Full : Representation<T>
{
  using Representation<T>::Representation;
};


// Filtered representation of Full<Framework>.
// Executors and Tasks are filtered based on whether the
// user is authorized to view them.
struct FullFrameworkWriter {
  FullFrameworkWriter(
      const process::Owned<ObjectApprovers>& approvers,
      const Framework* framework);

  void operator()(JSON::ObjectWriter* writer) const;

  const process::Owned<ObjectApprovers>& approvers_;
  const Framework* framework_;
};


struct SlaveWriter
{
  SlaveWriter(
      const Slave& slave,
      const process::Owned<ObjectApprovers>& approvers);

  void operator()(JSON::ObjectWriter* writer) const;

  const Slave& slave_;
  const process::Owned<ObjectApprovers>& approvers_;
};


struct SlavesWriter
{
  SlavesWriter(
      const Master::Slaves& slaves,
      const process::Owned<ObjectApprovers>& approvers,
      const IDAcceptor<SlaveID>& selectSlaveId);

  void operator()(JSON::ObjectWriter* writer) const;

  void writeSlave(const Slave* slave, JSON::ObjectWriter* writer) const;

  const Master::Slaves& slaves_;
  const process::Owned<ObjectApprovers>& approvers_;
  const IDAcceptor<SlaveID>& selectSlaveId_;
};


void json(JSON::ObjectWriter* writer, const Summary<Framework>& summary);


FullFrameworkWriter::FullFrameworkWriter(
    const Owned<ObjectApprovers>& approvers,
    const Framework* framework)
  : approvers_(approvers),
    framework_(framework)
{}


void FullFrameworkWriter::operator()(JSON::ObjectWriter* writer) const
{
  json(writer, Summary<Framework>(*framework_));

  // Add additional fields to those generated by the
  // `Summary<Framework>` overload.
  writer->field("user", framework_->info.user());
  writer->field("failover_timeout", framework_->info.failover_timeout());
  writer->field("checkpoint", framework_->info.checkpoint());
  writer->field("registered_time", framework_->registeredTime.secs());
  writer->field("unregistered_time", framework_->unregisteredTime.secs());

  if (framework_->info.has_principal()) {
    writer->field("principal", framework_->info.principal());
  }

  // TODO(bmahler): Consider deprecating this in favor of the split
  // used and offered resources added in `Summary<Framework>`.
  writer->field(
      "resources",
      framework_->totalUsedResources + framework_->totalOfferedResources);

  // TODO(benh): Consider making reregisteredTime an Option.
  if (framework_->registeredTime != framework_->reregisteredTime) {
    writer->field("reregistered_time", framework_->reregisteredTime.secs());
  }

  // For multi-role frameworks the `role` field will be unset.
  // Note that we could set `roles` here for both cases, which
  // would make tooling simpler (only need to look for `roles`).
  // However, we opted to just mirror the protobuf akin to how
  // generic protobuf -> JSON translation works.
  if (framework_->capabilities.multiRole) {
    writer->field("roles", framework_->info.roles());
  } else {
    writer->field("role", framework_->info.role());
  }

  // Model all of the tasks associated with a framework.
  writer->field("tasks", [this](JSON::ArrayWriter* writer) {
    foreachvalue (const TaskInfo& taskInfo, framework_->pendingTasks) {
      // Skip unauthorized tasks.
      if (!approvers_->approved<VIEW_TASK>(taskInfo, framework_->info)) {
        continue;
      }

      writer->element([this, &taskInfo](JSON::ObjectWriter* writer) {
        writer->field("id", taskInfo.task_id().value());
        writer->field("name", taskInfo.name());
        writer->field("framework_id", framework_->id().value());

        writer->field(
            "executor_id",
            taskInfo.executor().executor_id().value());

        writer->field("slave_id", taskInfo.slave_id().value());
        writer->field("state", TaskState_Name(TASK_STAGING));
        writer->field("resources", taskInfo.resources());

        // Tasks are not allowed to mix resources allocated to
        // different roles, see MESOS-6636.
        writer->field(
            "role",
            taskInfo.resources().begin()->allocation_info().role());

        writer->field("statuses", std::initializer_list<TaskStatus>{});

        if (taskInfo.has_labels()) {
          writer->field("labels", taskInfo.labels());
        }

        if (taskInfo.has_discovery()) {
          writer->field("discovery", JSON::Protobuf(taskInfo.discovery()));
        }

        if (taskInfo.has_container()) {
          writer->field("container", JSON::Protobuf(taskInfo.container()));
        }
      });
    }

    foreachvalue (Task* task, framework_->tasks) {
      // Skip unauthorized tasks.
      if (!approvers_->approved<VIEW_TASK>(*task, framework_->info)) {
        continue;
      }

      writer->element(*task);
    }
  });

  writer->field("unreachable_tasks", [this](JSON::ArrayWriter* writer) {
    foreachvalue (const Owned<Task>& task, framework_->unreachableTasks) {
      // Skip unauthorized tasks.
      if (!approvers_->approved<VIEW_TASK>(*task, framework_->info)) {
        continue;
      }

      writer->element(*task);
    }
  });

  writer->field("completed_tasks", [this](JSON::ArrayWriter* writer) {
    foreach (const Owned<Task>& task, framework_->completedTasks) {
      // Skip unauthorized tasks.
      if (!approvers_->approved<VIEW_TASK>(*task, framework_->info)) {
        continue;
      }

      writer->element(*task);
    }
  });

  // Model all of the offers associated with a framework.
  writer->field("offers", [this](JSON::ArrayWriter* writer) {
    foreach (Offer* offer, framework_->offers) {
      writer->element(*offer);
    }
  });

  // Model all of the executors of a framework.
  writer->field("executors", [this](JSON::ArrayWriter* writer) {
    foreachpair (
        const SlaveID& slaveId,
        const auto& executorsMap,
        framework_->executors) {
      foreachvalue (const ExecutorInfo& executor, executorsMap) {
        writer->element([this,
                         &executor,
                         &slaveId](JSON::ObjectWriter* writer) {
          // Skip unauthorized executors.
          if (!approvers_->approved<VIEW_EXECUTOR>(
                  executor, framework_->info)) {
            return;
          }

          json(writer, executor);
          writer->field("slave_id", slaveId.value());
        });
      }
    }
  });

  // Model all of the labels associated with a framework.
  if (framework_->info.has_labels()) {
    writer->field("labels", framework_->info.labels());
  }
}


SlaveWriter::SlaveWriter(
    const Slave& slave,
    const Owned<ObjectApprovers>& approvers)
  : slave_(slave), approvers_(approvers)
{}


void SlaveWriter::operator()(JSON::ObjectWriter* writer) const
{
  json(writer, slave_.info);

  writer->field("pid", string(slave_.pid));
  writer->field("registered_time", slave_.registeredTime.secs());

  if (slave_.reregisteredTime.isSome()) {
    writer->field("reregistered_time", slave_.reregisteredTime->secs());
  }

  const Resources& totalResources = slave_.totalResources;
  writer->field("resources", totalResources);
  writer->field("used_resources", Resources::sum(slave_.usedResources));
  writer->field("offered_resources", slave_.offeredResources);
  writer->field(
      "reserved_resources",
      [&totalResources, this](JSON::ObjectWriter* writer) {
        foreachpair (const string& role, const Resources& reservation,
                     totalResources.reservations()) {
          // TODO(arojas): Consider showing unapproved resources in an
          // aggregated special field, so that all resource values add up
          // MESOS-7779.
          if (approvers_->approved<VIEW_ROLE>(role)) {
            writer->field(role, reservation);
          }
        }
      });
  writer->field("unreserved_resources", totalResources.unreserved());

  writer->field("active", slave_.active);
  writer->field("version", slave_.version);
  writer->field("capabilities", slave_.capabilities.toRepeatedPtrField());
}


SlavesWriter::SlavesWriter(
    const Master::Slaves& slaves,
    const Owned<ObjectApprovers>& approvers,
    const IDAcceptor<SlaveID>& selectSlaveId)
  : slaves_(slaves), approvers_(approvers), selectSlaveId_(selectSlaveId)
{}


void SlavesWriter::operator()(JSON::ObjectWriter* writer) const
{
  writer->field("slaves", [this](JSON::ArrayWriter* writer) {
    foreachvalue (const Slave* slave, slaves_.registered) {
      if (!selectSlaveId_.accept(slave->id)) {
        continue;
      }

      writer->element([this, &slave](JSON::ObjectWriter* writer) {
        writeSlave(slave, writer);
      });
    }
  });

  writer->field("recovered_slaves", [this](JSON::ArrayWriter* writer) {
    foreachvalue (const SlaveInfo& slaveInfo, slaves_.recovered) {
      if (!selectSlaveId_.accept(slaveInfo.id())) {
        continue;
      }

      writer->element([&slaveInfo](JSON::ObjectWriter* writer) {
        json(writer, slaveInfo);
      });
    }
  });
}


void SlavesWriter::writeSlave(
  const Slave* slave, JSON::ObjectWriter* writer) const
{
  SlaveWriter(*slave, approvers_)(writer);

  // Add the complete protobuf->JSON for all used, reserved,
  // and offered resources. The other endpoints summarize
  // resource information, which omits the details of
  // reservations and persistent volumes. Full resource
  // information is necessary so that operators can use the
  // `/unreserve` and `/destroy-volumes` endpoints.

  hashmap<string, Resources> reserved = slave->totalResources.reservations();

  writer->field(
      "reserved_resources_full",
      [&reserved, this](JSON::ObjectWriter* writer) {
        foreachpair (const string& role,
                     const Resources& resources,
                     reserved) {
          if (approvers_->approved<VIEW_ROLE>(role)) {
            writer->field(role, [&resources, this](
                JSON::ArrayWriter* writer) {
              foreach (Resource resource, resources) {
                if (approvers_->approved<VIEW_ROLE>(resource)) {
                  convertResourceFormat(&resource, ENDPOINT);
                  writer->element(JSON::Protobuf(resource));
                }
              }
            });
          }
        }
      });

  Resources unreservedResources = slave->totalResources.unreserved();

  writer->field(
      "unreserved_resources_full",
      [&unreservedResources, this](JSON::ArrayWriter* writer) {
        foreach (Resource resource, unreservedResources) {
          if (approvers_->approved<VIEW_ROLE>(resource)) {
            convertResourceFormat(&resource, ENDPOINT);
            writer->element(JSON::Protobuf(resource));
          }
        }
      });

  Resources usedResources = Resources::sum(slave->usedResources);

  writer->field(
      "used_resources_full",
      [&usedResources, this](JSON::ArrayWriter* writer) {
        foreach (Resource resource, usedResources) {
          if (approvers_->approved<VIEW_ROLE>(resource)) {
            convertResourceFormat(&resource, ENDPOINT);
            writer->element(JSON::Protobuf(resource));
          }
        }
      });

  const Resources& offeredResources = slave->offeredResources;

  writer->field(
      "offered_resources_full",
      [&offeredResources, this](JSON::ArrayWriter* writer) {
        foreach (Resource resource, offeredResources) {
          if (approvers_->approved<VIEW_ROLE>(resource)) {
            convertResourceFormat(&resource, ENDPOINT);
            writer->element(JSON::Protobuf(resource));
          }
        }
      });
}


void json(JSON::ObjectWriter* writer, const Summary<Framework>& summary)
{
  const Framework& framework = summary;

  writer->field("id", framework.id().value());
  writer->field("name", framework.info.name());

  // Omit pid for http frameworks.
  if (framework.pid.isSome()) {
    writer->field("pid", string(framework.pid.get()));
  }

  // TODO(bmahler): Use these in the webui.
  writer->field("used_resources", framework.totalUsedResources);
  writer->field("offered_resources", framework.totalOfferedResources);
  writer->field("capabilities", framework.info.capabilities());
  writer->field("hostname", framework.info.hostname());
  writer->field("webui_url", framework.info.webui_url());
  writer->field("active", framework.active());
  writer->field("connected", framework.connected());
  writer->field("recovered", framework.recovered());
}


// This abstraction has no side-effects. It factors out computing the
// mapping from 'slaves' to 'frameworks' to answer the questions 'what
// frameworks are running on a given slave?' and 'what slaves are
// running the given framework?'.
class SlaveFrameworkMapping
{
public:
  SlaveFrameworkMapping(const hashmap<FrameworkID, Framework*>& frameworks)
  {
    foreachpair (const FrameworkID& frameworkId,
                 const Framework* framework,
                 frameworks) {
      foreachvalue (const TaskInfo& taskInfo, framework->pendingTasks) {
        frameworksToSlaves[frameworkId].insert(taskInfo.slave_id());
        slavesToFrameworks[taskInfo.slave_id()].insert(frameworkId);
      }

      foreachvalue (const Task* task, framework->tasks) {
        frameworksToSlaves[frameworkId].insert(task->slave_id());
        slavesToFrameworks[task->slave_id()].insert(frameworkId);
      }

      foreachvalue (const Owned<Task>& task, framework->unreachableTasks) {
        frameworksToSlaves[frameworkId].insert(task->slave_id());
        slavesToFrameworks[task->slave_id()].insert(frameworkId);
      }

      foreach (const Owned<Task>& task, framework->completedTasks) {
        frameworksToSlaves[frameworkId].insert(task->slave_id());
        slavesToFrameworks[task->slave_id()].insert(frameworkId);
      }
    }
  }

  const hashset<FrameworkID>& frameworks(const SlaveID& slaveId) const
  {
    const auto iterator = slavesToFrameworks.find(slaveId);
    return iterator != slavesToFrameworks.end() ?
      iterator->second : hashset<FrameworkID>::EMPTY;
  }

  const hashset<SlaveID>& slaves(const FrameworkID& frameworkId) const
  {
    const auto iterator = frameworksToSlaves.find(frameworkId);
    return iterator != frameworksToSlaves.end() ?
      iterator->second : hashset<SlaveID>::EMPTY;
  }

private:
  hashmap<SlaveID, hashset<FrameworkID>> slavesToFrameworks;
  hashmap<FrameworkID, hashset<SlaveID>> frameworksToSlaves;
};


// This abstraction has no side-effects. It factors out the accounting
// for a 'TaskState' summary. We use this to summarize 'TaskState's
// for both frameworks as well as slaves.
struct TaskStateSummary
{
  // TODO(jmlvanre): Possibly clean this up as per MESOS-2694.
  const static TaskStateSummary EMPTY;

  TaskStateSummary()
    : staging(0),
      starting(0),
      running(0),
      killing(0),
      finished(0),
      killed(0),
      failed(0),
      lost(0),
      error(0),
      dropped(0),
      unreachable(0),
      gone(0),
      gone_by_operator(0),
      unknown(0) {}

  // Account for the state of the given task.
  void count(const Task& task)
  {
    switch (task.state()) {
      case TASK_STAGING: { ++staging; break; }
      case TASK_STARTING: { ++starting; break; }
      case TASK_RUNNING: { ++running; break; }
      case TASK_KILLING: { ++killing; break; }
      case TASK_FINISHED: { ++finished; break; }
      case TASK_KILLED: { ++killed; break; }
      case TASK_FAILED: { ++failed; break; }
      case TASK_LOST: { ++lost; break; }
      case TASK_ERROR: { ++error; break; }
      case TASK_DROPPED: { ++dropped; break; }
      case TASK_UNREACHABLE: { ++unreachable; break; }
      case TASK_GONE: { ++gone; break; }
      case TASK_GONE_BY_OPERATOR: { ++gone_by_operator; break; }
      case TASK_UNKNOWN: { ++unknown; break; }
      // No default case allows for a helpful compiler error if we
      // introduce a new state.
    }
  }

  size_t staging;
  size_t starting;
  size_t running;
  size_t killing;
  size_t finished;
  size_t killed;
  size_t failed;
  size_t lost;
  size_t error;
  size_t dropped;
  size_t unreachable;
  size_t gone;
  size_t gone_by_operator;
  size_t unknown;
};


const TaskStateSummary TaskStateSummary::EMPTY;


// This abstraction has no side-effects. It factors out computing the
// 'TaskState' summaries for frameworks and slaves. This answers the
// questions 'How many tasks are in each state for a given framework?'
// and 'How many tasks are in each state for a given slave?'.
class TaskStateSummaries
{
public:
  TaskStateSummaries(const hashmap<FrameworkID, Framework*>& frameworks)
  {
    foreachpair (const FrameworkID& frameworkId,
                 const Framework* framework,
                 frameworks) {
      foreachvalue (const TaskInfo& taskInfo, framework->pendingTasks) {
        frameworkTaskSummaries[frameworkId].staging++;
        slaveTaskSummaries[taskInfo.slave_id()].staging++;
      }

      foreachvalue (const Task* task, framework->tasks) {
        frameworkTaskSummaries[frameworkId].count(*task);
        slaveTaskSummaries[task->slave_id()].count(*task);
      }

      foreachvalue (const Owned<Task>& task, framework->unreachableTasks) {
        frameworkTaskSummaries[frameworkId].count(*task);
        slaveTaskSummaries[task->slave_id()].count(*task);
      }

      foreach (const Owned<Task>& task, framework->completedTasks) {
        frameworkTaskSummaries[frameworkId].count(*task);
        slaveTaskSummaries[task->slave_id()].count(*task);
      }
    }
  }

  const TaskStateSummary& framework(const FrameworkID& frameworkId) const
  {
    const auto iterator = frameworkTaskSummaries.find(frameworkId);
    return iterator != frameworkTaskSummaries.end() ?
      iterator->second : TaskStateSummary::EMPTY;
  }

  const TaskStateSummary& slave(const SlaveID& slaveId) const
  {
    const auto iterator = slaveTaskSummaries.find(slaveId);
    return iterator != slaveTaskSummaries.end() ?
      iterator->second : TaskStateSummary::EMPTY;
  }

private:
  hashmap<FrameworkID, TaskStateSummary> frameworkTaskSummaries;
  hashmap<SlaveID, TaskStateSummary> slaveTaskSummaries;
};


process::http::Response Master::ReadOnlyHandler::frameworks(
    const process::http::Request& request,
    const process::Owned<ObjectApprovers>& approvers) const
{
  IDAcceptor<FrameworkID> selectFrameworkId(
      request.url.query.get("framework_id"));

  // This lambda is consumed before the outer lambda
  // returns, hence capture by reference is fine here.
  const Master* master = this->master;
  auto frameworks = [master, &approvers, &selectFrameworkId](
      JSON::ObjectWriter* writer) {
    // Model all of the frameworks.
    writer->field(
        "frameworks",
        [master, &approvers, &selectFrameworkId](
            JSON::ArrayWriter* writer) {
          foreachvalue (
              Framework* framework, master->frameworks.registered) {
            // Skip unauthorized frameworks or frameworks
            // without a matching ID.
            if (!selectFrameworkId.accept(framework->id()) ||
                !approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
              continue;
            }

            writer->element(FullFrameworkWriter(approvers, framework));
          }
        });

    // Model all of the completed frameworks.
    writer->field(
        "completed_frameworks",
        [master, &approvers, &selectFrameworkId](
            JSON::ArrayWriter* writer) {
          foreachvalue (const Owned<Framework>& framework,
                        master->frameworks.completed) {
            // Skip unauthorized frameworks or frameworks
            // without a matching ID.
            if (!selectFrameworkId.accept(framework->id()) ||
                !approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
              continue;
            }

            writer->element(
                FullFrameworkWriter(approvers, framework.get()));
          }
        });

    // Unregistered frameworks are no longer possible. We emit an
    // empty array for the sake of backward compatibility.
    writer->field("unregistered_frameworks", [](JSON::ArrayWriter*) {});
  };

  return OK(jsonify(frameworks), request.url.query.get("jsonp"));
}


process::http::Response Master::ReadOnlyHandler::slaves(
    const process::http::Request& request,
    const process::Owned<ObjectApprovers>& approvers) const
{
  Option<string> slaveId = request.url.query.get("slave_id");
  IDAcceptor<SlaveID> selectSlaveId(slaveId);

  return process::http::OK(
      jsonify(SlavesWriter(master->slaves, approvers, selectSlaveId)),
      request.url.query.get("jsonp"));
}


process::http::Response Master::ReadOnlyHandler::state(
    const process::http::Request& request,
    const process::Owned<ObjectApprovers>& approvers) const
{
  const Master* master = this->master;
  auto calculateState = [master, &approvers](JSON::ObjectWriter* writer) {
    writer->field("version", MESOS_VERSION);

    if (build::GIT_SHA.isSome()) {
      writer->field("git_sha", build::GIT_SHA.get());
    }

    if (build::GIT_BRANCH.isSome()) {
      writer->field("git_branch", build::GIT_BRANCH.get());
    }

    if (build::GIT_TAG.isSome()) {
      writer->field("git_tag", build::GIT_TAG.get());
    }

    writer->field("build_date", build::DATE);
    writer->field("build_time", build::TIME);
    writer->field("build_user", build::USER);
    writer->field("start_time", master->startTime.secs());

    if (master->electedTime.isSome()) {
      writer->field("elected_time", master->electedTime->secs());
    }

    writer->field("id", master->info().id());
    writer->field("pid", string(master->self()));
    writer->field("hostname", master->info().hostname());
    writer->field("capabilities", master->info().capabilities());
    writer->field("activated_slaves", master->_const_slaves_active());
    writer->field("deactivated_slaves", master->_const_slaves_inactive());
    writer->field("unreachable_slaves", master->_const_slaves_unreachable());

    if (master->info().has_domain()) {
      writer->field("domain", master->info().domain());
    }

    // TODO(haosdent): Deprecated this in favor of `leader_info` below.
    if (master->leader.isSome()) {
      writer->field("leader", master->leader->pid());
    }

    if (master->leader.isSome()) {
      writer->field("leader_info", [master](JSON::ObjectWriter* writer) {
        json(writer, master->leader.get());
      });
    }

    if (approvers->approved<VIEW_FLAGS>()) {
      if (master->flags.cluster.isSome()) {
        writer->field("cluster", master->flags.cluster.get());
      }

      if (master->flags.log_dir.isSome()) {
        writer->field("log_dir", master->flags.log_dir.get());
      }

      if (master->flags.external_log_file.isSome()) {
        writer->field("external_log_file",
                      master->flags.external_log_file.get());
      }

      writer->field("flags", [master](JSON::ObjectWriter* writer) {
          foreachvalue (const flags::Flag& flag, master->flags) {
            Option<string> value = flag.stringify(master->flags);
            if (value.isSome()) {
              writer->field(flag.effective_name().value, value.get());
            }
          }
        });
    }

    // Model all of the registered slaves.
    writer->field(
        "slaves",
        [master, &approvers](JSON::ArrayWriter* writer) {
          foreachvalue (Slave* slave, master->slaves.registered) {
            writer->element(SlaveWriter(*slave, approvers));
          }
        });

    // Model all of the recovered slaves.
    writer->field(
        "recovered_slaves",
        [master](JSON::ArrayWriter* writer) {
          foreachvalue (
              const SlaveInfo& slaveInfo, master->slaves.recovered) {
            writer->element([&slaveInfo](JSON::ObjectWriter* writer) {
              json(writer, slaveInfo);
            });
          }
        });

    // Model all of the frameworks.
    writer->field(
        "frameworks",
        [master, &approvers](JSON::ArrayWriter* writer) {
          foreachvalue (
              Framework* framework, master->frameworks.registered) {
            // Skip unauthorized frameworks.
            if (!approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
              continue;
            }

            writer->element(FullFrameworkWriter(approvers, framework));
          }
        });

    // Model all of the completed frameworks.
    writer->field(
        "completed_frameworks",
        [master, &approvers](JSON::ArrayWriter* writer) {
          foreachvalue (
              const Owned<Framework>& framework,
              master->frameworks.completed) {
            // Skip unauthorized frameworks.
            if (!approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
              continue;
            }

            writer->element(
                FullFrameworkWriter(approvers, framework.get()));
          }
        });

    // Orphan tasks are no longer possible. We emit an empty array
    // for the sake of backward compatibility.
    writer->field("orphan_tasks", [](JSON::ArrayWriter*) {});

    // Unregistered frameworks are no longer possible. We emit an
    // empty array for the sake of backward compatibility.
    writer->field("unregistered_frameworks", [](JSON::ArrayWriter*) {});
  };

  return OK(jsonify(calculateState), request.url.query.get("jsonp"));
}


process::http::Response Master::ReadOnlyHandler::stateSummary(
    const process::http::Request& request,
    const process::Owned<ObjectApprovers>& approvers) const
{
  const Master* master = this->master;
  auto stateSummary = [master, &approvers](JSON::ObjectWriter* writer) {
    writer->field("hostname", master->info().hostname());

    if (master->flags.cluster.isSome()) {
      writer->field("cluster", master->flags.cluster.get());
    }

    // We use the tasks in the 'Frameworks' struct to compute summaries
    // for this endpoint. This is done 1) for consistency between the
    // 'slaves' and 'frameworks' subsections below 2) because we want to
    // provide summary information for frameworks that are currently
    // registered 3) the frameworks keep a circular buffer of completed
    // tasks that we can use to keep a limited view on the history of
    // recent completed / failed tasks.

    // Generate mappings from 'slave' to 'framework' and reverse.
    SlaveFrameworkMapping slaveFrameworkMapping(
        master->frameworks.registered);

    // Generate 'TaskState' summaries for all framework and slave ids.
    TaskStateSummaries taskStateSummaries(
        master->frameworks.registered);

    // Model all of the slaves.
    writer->field(
        "slaves",
        [master,
         &slaveFrameworkMapping,
         &taskStateSummaries,
         &approvers](JSON::ArrayWriter* writer) {
          foreachvalue (Slave* slave, master->slaves.registered) {
            writer->element(
                [&slave,
                 &slaveFrameworkMapping,
                 &taskStateSummaries,
                 &approvers](JSON::ObjectWriter* writer) {
                  SlaveWriter slaveWriter(*slave, approvers);
                  slaveWriter(writer);

                  // Add the 'TaskState' summary for this slave.
                  const TaskStateSummary& summary =
                      taskStateSummaries.slave(slave->id);

                  // Certain per-agent status totals will always be zero
                  // (e.g., TASK_ERROR, TASK_UNREACHABLE). We report
                  // them here anyway, for completeness.
                  //
                  // TODO(neilc): Update for TASK_GONE and
                  // TASK_GONE_BY_OPERATOR.
                  writer->field("TASK_STAGING", summary.staging);
                  writer->field("TASK_STARTING", summary.starting);
                  writer->field("TASK_RUNNING", summary.running);
                  writer->field("TASK_KILLING", summary.killing);
                  writer->field("TASK_FINISHED", summary.finished);
                  writer->field("TASK_KILLED", summary.killed);
                  writer->field("TASK_FAILED", summary.failed);
                  writer->field("TASK_LOST", summary.lost);
                  writer->field("TASK_ERROR", summary.error);
                  writer->field(
                      "TASK_UNREACHABLE",
                      summary.unreachable);

                  // Add the ids of all the frameworks running on this
                  // slave.
                  const hashset<FrameworkID>& frameworks =
                      slaveFrameworkMapping.frameworks(slave->id);

                  writer->field(
                      "framework_ids",
                      [&frameworks](JSON::ArrayWriter* writer) {
                        foreach (
                            const FrameworkID& frameworkId,
                            frameworks) {
                          writer->element(frameworkId.value());
                        }
                      });
                });
          }
        });

    // Model all of the frameworks.
    writer->field(
        "frameworks",
        [master,
         &slaveFrameworkMapping,
         &taskStateSummaries,
         &approvers](JSON::ArrayWriter* writer) {
          foreachpair (const FrameworkID& frameworkId,
                       Framework* framework,
                       master->frameworks.registered) {
            // Skip unauthorized frameworks.
            if (!approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
              continue;
            }

            writer->element(
                [&frameworkId,
                 &framework,
                 &slaveFrameworkMapping,
                 &taskStateSummaries](JSON::ObjectWriter* writer) {
                  json(writer, Summary<Framework>(*framework));

                  // Add the 'TaskState' summary for this framework.
                  const TaskStateSummary& summary =
                      taskStateSummaries.framework(frameworkId);

                  // TODO(neilc): Update for TASK_GONE and
                  // TASK_GONE_BY_OPERATOR.
                  writer->field("TASK_STAGING", summary.staging);
                  writer->field("TASK_STARTING", summary.starting);
                  writer->field("TASK_RUNNING", summary.running);
                  writer->field("TASK_KILLING", summary.killing);
                  writer->field("TASK_FINISHED", summary.finished);
                  writer->field("TASK_KILLED", summary.killed);
                  writer->field("TASK_FAILED", summary.failed);
                  writer->field("TASK_LOST", summary.lost);
                  writer->field("TASK_ERROR", summary.error);
                  writer->field(
                      "TASK_UNREACHABLE",
                      summary.unreachable);

                  // Add the ids of all the slaves running
                  // this framework.
                  const hashset<SlaveID>& slaves =
                      slaveFrameworkMapping.slaves(frameworkId);

                  writer->field(
                      "slave_ids",
                      [&slaves](JSON::ArrayWriter* writer) {
                        foreach (const SlaveID& slaveId, slaves) {
                          writer->element(slaveId.value());
                        }
                      });
                });
          }
        });
    };

  return OK(jsonify(stateSummary), request.url.query.get("jsonp"));
}


struct TaskComparator
{
  static bool ascending(const Task* lhs, const Task* rhs)
  {
    size_t lhsSize = lhs->statuses().size();
    size_t rhsSize = rhs->statuses().size();

    if ((lhsSize == 0) && (rhsSize == 0)) {
      return false;
    }

    if (lhsSize == 0) {
      return true;
    }

    if (rhsSize == 0) {
      return false;
    }

    return (lhs->statuses(0).timestamp() < rhs->statuses(0).timestamp());
  }

  static bool descending(const Task* lhs, const Task* rhs)
  {
    size_t lhsSize = lhs->statuses().size();
    size_t rhsSize = rhs->statuses().size();

    if ((lhsSize == 0) && (rhsSize == 0)) {
      return false;
    }

    if (rhsSize == 0) {
      return true;
    }

    if (lhsSize == 0) {
      return false;
    }

    return (lhs->statuses(0).timestamp() > rhs->statuses(0).timestamp());
  }
};


process::http::Response Master::ReadOnlyHandler::tasks(
  const process::http::Request& request,
  const process::Owned<ObjectApprovers>& approvers) const
{
  // Get list options (limit and offset).
  Result<int> result = numify<int>(request.url.query.get("limit"));
  size_t limit = result.isSome() ? result.get() : TASK_LIMIT;

  result = numify<int>(request.url.query.get("offset"));
  size_t offset = result.isSome() ? result.get() : 0;

  Option<string> order = request.url.query.get("order");
  string _order = order.isSome() && (order.get() == "asc") ? "asc" : "des";

  Option<string> frameworkId = request.url.query.get("framework_id");
  Option<string> taskId = request.url.query.get("task_id");

  IDAcceptor<FrameworkID> selectFrameworkId(frameworkId);
  IDAcceptor<TaskID> selectTaskId(taskId);

  // Construct framework list with both active and completed frameworks.
  vector<const Framework*> frameworks;
  foreachvalue (Framework* framework, master->frameworks.registered) {
    // Skip unauthorized frameworks or frameworks without matching
    // framework ID.
    if (!selectFrameworkId.accept(framework->id()) ||
        !approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
      continue;
    }

    frameworks.push_back(framework);
  }

  foreachvalue (const Owned<Framework>& framework,
                master->frameworks.completed) {
    // Skip unauthorized frameworks or frameworks without matching
    // framework ID.
    if (!selectFrameworkId.accept(framework->id()) ||
        !approvers->approved<VIEW_FRAMEWORK>(framework->info)) {
     continue;
    }

    frameworks.push_back(framework.get());
  }

  // Construct task list with both running,
  // completed and unreachable tasks.
  vector<const Task*> tasks;
  foreach (const Framework* framework, frameworks) {
    foreachvalue (Task* task, framework->tasks) {
      CHECK_NOTNULL(task);
      // Skip unauthorized tasks or tasks without matching task ID.
      if (!selectTaskId.accept(task->task_id()) ||
          !approvers->approved<VIEW_TASK>(*task, framework->info)) {
        continue;
      }

      tasks.push_back(task);
    }

    foreachvalue (
        const Owned<Task>& task,
        framework->unreachableTasks) {
      // Skip unauthorized tasks or tasks without matching task ID.
      if (!selectTaskId.accept(task->task_id()) ||
          !approvers->approved<VIEW_TASK>(*task, framework->info)) {
        continue;
      }

      tasks.push_back(task.get());
    }

    foreach (const Owned<Task>& task, framework->completedTasks) {
      // Skip unauthorized tasks or tasks without matching task ID.
      if (!selectTaskId.accept(task->task_id()) ||
          !approvers->approved<VIEW_TASK>(*task, framework->info)) {
        continue;
      }

      tasks.push_back(task.get());
    }
  }

  // Sort tasks by task status timestamp. Default order is descending.
  // The earliest timestamp is chosen for comparison when
  // multiple are present.
  if (_order == "asc") {
    sort(tasks.begin(), tasks.end(), TaskComparator::ascending);
  } else {
    sort(tasks.begin(), tasks.end(), TaskComparator::descending);
  }

  auto tasksWriter =
    [&tasks, limit, offset](JSON::ObjectWriter* writer) {
      writer->field(
          "tasks",
          [&tasks, limit, offset](JSON::ArrayWriter* writer) {
            // Collect 'limit' number of tasks starting from 'offset'.
            size_t end = std::min(offset + limit, tasks.size());
            for (size_t i = offset; i < end; i++) {
              writer->element(*tasks[i]);
            }
          });
  };

  return OK(jsonify(tasksWriter), request.url.query.get("jsonp"));
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
