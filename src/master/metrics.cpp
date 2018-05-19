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
// limitations under the License.

#include <string>

#include <process/metrics/counter.hpp>
#include <process/metrics/pull_gauge.hpp>
#include <process/metrics/push_gauge.hpp>
#include <process/metrics/metrics.hpp>

#include <stout/duration.hpp>
#include <stout/foreach.hpp>

#include "master/master.hpp"
#include "master/metrics.hpp"

using process::metrics::Counter;
using process::metrics::PullGauge;
using process::metrics::PushGauge;

using std::string;

namespace mesos {
namespace internal {
namespace master {

// Message counters are named with "messages_" prefix so they can
// be grouped together alphabetically in the output.
// TODO(alexandra.sava): Add metrics for registered and removed slaves.
Metrics::Metrics(const Master& master)
  : uptime_secs(
        "master/uptime_secs",
        defer(master, &Master::_uptime_secs)),
    elected(
        "master/elected",
        defer(master, &Master::_elected)),
    slaves_connected(
        "master/slaves_connected",
        defer(master, &Master::_slaves_connected)),
    slaves_disconnected(
        "master/slaves_disconnected",
        defer(master, &Master::_slaves_disconnected)),
    slaves_active(
        "master/slaves_active",
        defer(master, &Master::_slaves_active)),
    slaves_inactive(
        "master/slaves_inactive",
        defer(master, &Master::_slaves_inactive)),
    slaves_unreachable(
        "master/slaves_unreachable",
        defer(master, &Master::_slaves_unreachable)),
    frameworks_connected(
        "master/frameworks_connected",
        defer(master, &Master::_frameworks_connected)),
    frameworks_disconnected(
        "master/frameworks_disconnected",
        defer(master, &Master::_frameworks_disconnected)),
    frameworks_active(
        "master/frameworks_active",
        defer(master, &Master::_frameworks_active)),
    frameworks_inactive(
        "master/frameworks_inactive",
        defer(master, &Master::_frameworks_inactive)),
    outstanding_offers(
        "master/outstanding_offers",
        defer(master, &Master::_outstanding_offers)),
    tasks_staging(
        "master/tasks_staging",
        defer(master, &Master::_tasks_staging)),
    tasks_starting(
        "master/tasks_starting",
        defer(master, &Master::_tasks_starting)),
    tasks_running(
        "master/tasks_running",
        defer(master, &Master::_tasks_running)),
    tasks_unreachable(
        "master/tasks_unreachable",
        defer(master, &Master::_tasks_unreachable)),
    tasks_killing(
        "master/tasks_killing",
        defer(master, &Master::_tasks_killing)),
    tasks_finished(
        "master/tasks_finished"),
    tasks_failed(
        "master/tasks_failed"),
    tasks_killed(
        "master/tasks_killed"),
    tasks_lost(
        "master/tasks_lost"),
    tasks_error(
        "master/tasks_error"),
    tasks_dropped(
        "master/tasks_dropped"),
    tasks_gone(
        "master/tasks_gone"),
    tasks_gone_by_operator(
        "master/tasks_gone_by_operator"),
    dropped_messages(
        "master/dropped_messages"),
    messages_register_framework(
        "master/messages_register_framework"),
    messages_reregister_framework(
        "master/messages_reregister_framework"),
    messages_unregister_framework(
        "master/messages_unregister_framework"),
    messages_deactivate_framework(
        "master/messages_deactivate_framework"),
    messages_kill_task(
        "master/messages_kill_task"),
    messages_status_update_acknowledgement(
        "master/messages_status_update_acknowledgement"),
    messages_resource_request(
        "master/messages_resource_request"),
    messages_launch_tasks(
        "master/messages_launch_tasks"),
    messages_decline_offers(
        "master/messages_decline_offers"),
    messages_revive_offers(
        "master/messages_revive_offers"),
    messages_suppress_offers(
        "master/messages_suppress_offers"),
    messages_reconcile_tasks(
        "master/messages_reconcile_tasks"),
    messages_framework_to_executor(
        "master/messages_framework_to_executor"),
    messages_executor_to_framework(
        "master/messages_executor_to_framework"),
    messages_register_slave(
        "master/messages_register_slave"),
    messages_reregister_slave(
        "master/messages_reregister_slave"),
    messages_unregister_slave(
        "master/messages_unregister_slave"),
    messages_status_update(
        "master/messages_status_update"),
    messages_exited_executor(
        "master/messages_exited_executor"),
    messages_update_slave(
        "master/messages_update_slave"),
    messages_authenticate(
        "master/messages_authenticate"),
    valid_framework_to_executor_messages(
        "master/valid_framework_to_executor_messages"),
    invalid_framework_to_executor_messages(
        "master/invalid_framework_to_executor_messages"),
    valid_executor_to_framework_messages(
        "master/valid_executor_to_framework_messages"),
    invalid_executor_to_framework_messages(
        "master/invalid_executor_to_framework_messages"),
    valid_status_updates(
        "master/valid_status_updates"),
    invalid_status_updates(
        "master/invalid_status_updates"),
    valid_status_update_acknowledgements(
        "master/valid_status_update_acknowledgements"),
    invalid_status_update_acknowledgements(
        "master/invalid_status_update_acknowledgements"),
    recovery_slave_removals(
        "master/recovery_slave_removals"),
    event_queue_messages(
        "master/event_queue_messages",
        defer(master, &Master::_event_queue_messages)),
    event_queue_dispatches(
        "master/event_queue_dispatches",
        defer(master, &Master::_event_queue_dispatches)),
    event_queue_http_requests(
        "master/event_queue_http_requests",
        defer(master, &Master::_event_queue_http_requests)),
    slave_registrations(
        "master/slave_registrations"),
    slave_reregistrations(
        "master/slave_reregistrations"),
    slave_removals(
        "master/slave_removals"),
    slave_removals_reason_unhealthy(
        "master/slave_removals/reason_unhealthy"),
    slave_removals_reason_unregistered(
        "master/slave_removals/reason_unregistered"),
    slave_removals_reason_registered(
        "master/slave_removals/reason_registered"),
    slave_shutdowns_scheduled(
        "master/slave_shutdowns_scheduled"),
    slave_shutdowns_completed(
        "master/slave_shutdowns_completed"),
    slave_shutdowns_canceled(
        "master/slave_shutdowns_canceled"),
    slave_unreachable_scheduled(
        "master/slave_unreachable_scheduled"),
    slave_unreachable_completed(
        "master/slave_unreachable_completed"),
    slave_unreachable_canceled(
        "master/slave_unreachable_canceled")
{
  // TODO(dhamon): Check return values of 'add'.
  process::metrics::add(uptime_secs);
  process::metrics::add(elected);

  process::metrics::add(slaves_connected);
  process::metrics::add(slaves_disconnected);
  process::metrics::add(slaves_active);
  process::metrics::add(slaves_inactive);
  process::metrics::add(slaves_unreachable);

  process::metrics::add(frameworks_connected);
  process::metrics::add(frameworks_disconnected);
  process::metrics::add(frameworks_active);
  process::metrics::add(frameworks_inactive);

  process::metrics::add(outstanding_offers);

  process::metrics::add(tasks_staging);
  process::metrics::add(tasks_starting);
  process::metrics::add(tasks_running);
  process::metrics::add(tasks_killing);
  process::metrics::add(tasks_finished);
  process::metrics::add(tasks_failed);
  process::metrics::add(tasks_killed);
  process::metrics::add(tasks_lost);
  process::metrics::add(tasks_error);
  process::metrics::add(tasks_dropped);
  process::metrics::add(tasks_unreachable);
  process::metrics::add(tasks_gone);
  process::metrics::add(tasks_gone_by_operator);

  process::metrics::add(dropped_messages);

  // Messages from schedulers.
  process::metrics::add(messages_register_framework);
  process::metrics::add(messages_reregister_framework);
  process::metrics::add(messages_unregister_framework);
  process::metrics::add(messages_deactivate_framework);
  process::metrics::add(messages_kill_task);
  process::metrics::add(messages_status_update_acknowledgement);
  process::metrics::add(messages_resource_request);
  process::metrics::add(messages_launch_tasks);
  process::metrics::add(messages_decline_offers);
  process::metrics::add(messages_revive_offers);
  process::metrics::add(messages_suppress_offers);
  process::metrics::add(messages_reconcile_tasks);
  process::metrics::add(messages_framework_to_executor);
  process::metrics::add(messages_executor_to_framework);

  // Messages from slaves.
  process::metrics::add(messages_register_slave);
  process::metrics::add(messages_reregister_slave);
  process::metrics::add(messages_unregister_slave);
  process::metrics::add(messages_status_update);
  process::metrics::add(messages_exited_executor);
  process::metrics::add(messages_update_slave);

  // Messages from both schedulers and slaves.
  process::metrics::add(messages_authenticate);

  process::metrics::add(valid_framework_to_executor_messages);
  process::metrics::add(invalid_framework_to_executor_messages);

  process::metrics::add(valid_executor_to_framework_messages);
  process::metrics::add(invalid_executor_to_framework_messages);

  process::metrics::add(valid_status_updates);
  process::metrics::add(invalid_status_updates);

  process::metrics::add(valid_status_update_acknowledgements);
  process::metrics::add(invalid_status_update_acknowledgements);

  process::metrics::add(recovery_slave_removals);

  process::metrics::add(event_queue_messages);
  process::metrics::add(event_queue_dispatches);
  process::metrics::add(event_queue_http_requests);

  process::metrics::add(slave_registrations);
  process::metrics::add(slave_reregistrations);
  process::metrics::add(slave_removals);
  process::metrics::add(slave_removals_reason_unhealthy);
  process::metrics::add(slave_removals_reason_unregistered);
  process::metrics::add(slave_removals_reason_registered);

  process::metrics::add(slave_shutdowns_scheduled);
  process::metrics::add(slave_shutdowns_completed);
  process::metrics::add(slave_shutdowns_canceled);

  process::metrics::add(slave_unreachable_scheduled);
  process::metrics::add(slave_unreachable_completed);
  process::metrics::add(slave_unreachable_canceled);

  // Create resource gauges.
  // TODO(dhamon): Set these up dynamically when adding a slave based on the
  // resources the slave exposes.
  const string resources[] = {"cpus", "gpus", "mem", "disk"};

  foreach (const string& resource, resources) {
    PullGauge total(
        "master/" + resource + "_total",
        defer(master, &Master::_resources_total, resource));

    PullGauge used(
        "master/" + resource + "_used",
        defer(master, &Master::_resources_used, resource));

    PullGauge percent(
        "master/" + resource + "_percent",
        defer(master, &Master::_resources_percent, resource));

    resources_total.push_back(total);
    resources_used.push_back(used);
    resources_percent.push_back(percent);

    process::metrics::add(total);
    process::metrics::add(used);
    process::metrics::add(percent);
  }

  foreach (const string& resource, resources) {
    PullGauge total(
        "master/" + resource + "_revocable_total",
        defer(master, &Master::_resources_revocable_total, resource));

    PullGauge used(
        "master/" + resource + "_revocable_used",
        defer(master, &Master::_resources_revocable_used, resource));

    PullGauge percent(
        "master/" + resource + "_revocable_percent",
        defer(master, &Master::_resources_revocable_percent, resource));

    resources_revocable_total.push_back(total);
    resources_revocable_used.push_back(used);
    resources_revocable_percent.push_back(percent);

    process::metrics::add(total);
    process::metrics::add(used);
    process::metrics::add(percent);
  }
}


Metrics::~Metrics()
{
  // TODO(dhamon): Check return values of 'remove'.
  process::metrics::remove(uptime_secs);
  process::metrics::remove(elected);

  process::metrics::remove(slaves_connected);
  process::metrics::remove(slaves_disconnected);
  process::metrics::remove(slaves_active);
  process::metrics::remove(slaves_inactive);
  process::metrics::remove(slaves_unreachable);

  process::metrics::remove(frameworks_connected);
  process::metrics::remove(frameworks_disconnected);
  process::metrics::remove(frameworks_active);
  process::metrics::remove(frameworks_inactive);

  process::metrics::remove(outstanding_offers);

  process::metrics::remove(tasks_staging);
  process::metrics::remove(tasks_starting);
  process::metrics::remove(tasks_running);
  process::metrics::remove(tasks_killing);
  process::metrics::remove(tasks_finished);
  process::metrics::remove(tasks_failed);
  process::metrics::remove(tasks_killed);
  process::metrics::remove(tasks_lost);
  process::metrics::remove(tasks_error);
  process::metrics::remove(tasks_dropped);
  process::metrics::remove(tasks_unreachable);
  process::metrics::remove(tasks_gone);
  process::metrics::remove(tasks_gone_by_operator);

  process::metrics::remove(dropped_messages);

  // Messages from schedulers.
  process::metrics::remove(messages_register_framework);
  process::metrics::remove(messages_reregister_framework);
  process::metrics::remove(messages_unregister_framework);
  process::metrics::remove(messages_deactivate_framework);
  process::metrics::remove(messages_kill_task);
  process::metrics::remove(messages_status_update_acknowledgement);
  process::metrics::remove(messages_resource_request);
  process::metrics::remove(messages_launch_tasks);
  process::metrics::remove(messages_decline_offers);
  process::metrics::remove(messages_revive_offers);
  process::metrics::remove(messages_suppress_offers);
  process::metrics::remove(messages_reconcile_tasks);
  process::metrics::remove(messages_framework_to_executor);
  process::metrics::remove(messages_executor_to_framework);

  // Messages from slaves.
  process::metrics::remove(messages_register_slave);
  process::metrics::remove(messages_reregister_slave);
  process::metrics::remove(messages_unregister_slave);
  process::metrics::remove(messages_status_update);
  process::metrics::remove(messages_exited_executor);
  process::metrics::remove(messages_update_slave);

  // Messages from both schedulers and slaves.
  process::metrics::remove(messages_authenticate);

  process::metrics::remove(valid_framework_to_executor_messages);
  process::metrics::remove(invalid_framework_to_executor_messages);

  process::metrics::remove(valid_executor_to_framework_messages);
  process::metrics::remove(invalid_executor_to_framework_messages);

  process::metrics::remove(valid_status_updates);
  process::metrics::remove(invalid_status_updates);

  process::metrics::remove(valid_status_update_acknowledgements);
  process::metrics::remove(invalid_status_update_acknowledgements);

  process::metrics::remove(recovery_slave_removals);

  process::metrics::remove(event_queue_messages);
  process::metrics::remove(event_queue_dispatches);
  process::metrics::remove(event_queue_http_requests);

  process::metrics::remove(slave_registrations);
  process::metrics::remove(slave_reregistrations);
  process::metrics::remove(slave_removals);
  process::metrics::remove(slave_removals_reason_unhealthy);
  process::metrics::remove(slave_removals_reason_unregistered);
  process::metrics::remove(slave_removals_reason_registered);

  process::metrics::remove(slave_shutdowns_scheduled);
  process::metrics::remove(slave_shutdowns_completed);
  process::metrics::remove(slave_shutdowns_canceled);

  process::metrics::remove(slave_unreachable_scheduled);
  process::metrics::remove(slave_unreachable_completed);
  process::metrics::remove(slave_unreachable_canceled);

  foreach (const PullGauge& gauge, resources_total) {
    process::metrics::remove(gauge);
  }
  resources_total.clear();

  foreach (const PullGauge& gauge, resources_used) {
    process::metrics::remove(gauge);
  }
  resources_used.clear();

  foreach (const PullGauge& gauge, resources_percent) {
    process::metrics::remove(gauge);
  }
  resources_percent.clear();

  foreach (const PullGauge& gauge, resources_revocable_total) {
    process::metrics::remove(gauge);
  }
  resources_revocable_total.clear();

  foreach (const PullGauge& gauge, resources_revocable_used) {
    process::metrics::remove(gauge);
  }
  resources_revocable_used.clear();

  foreach (const PullGauge& gauge, resources_revocable_percent) {
    process::metrics::remove(gauge);
  }
  resources_revocable_percent.clear();

  foreachvalue (const auto& source_reason, tasks_states) {
    foreachvalue (const auto& reason_counter, source_reason) {
      foreachvalue (const Counter& counter, reason_counter) {
        process::metrics::remove(counter);
      }
    }
  }
  tasks_states.clear();
}


void Metrics::incrementTasksStates(
    const TaskState& state,
    const TaskStatus::Source& source,
    const TaskStatus::Reason& reason)
{
  if (!tasks_states.contains(state)) {
    tasks_states[state] = SourcesReasons();
  }

  if (!tasks_states[state].contains(source)) {
    tasks_states[state][source] = Reasons();
  }

  if (!tasks_states[state][source].contains(reason)) {
    Counter counter = Counter(
        "master/" +
        strings::lower(TaskState_Name(state)) + "/" +
        strings::lower(TaskStatus::Source_Name(source)) + "/" +
        strings::lower(TaskStatus::Reason_Name(reason)));

    tasks_states[state][source].put(reason, counter);
    process::metrics::add(counter);
  }

  Counter counter = tasks_states[state][source].get(reason).get();
  counter++;
}


FrameworkMetrics::FrameworkMetrics(
    const Master& master,
    const FrameworkInfo& _frameworkInfo)
  : frameworkInfo(_frameworkInfo),
    subscribed(
        getPrefix(frameworkInfo) + "subscribed"),
    calls(
        getPrefix(frameworkInfo) + "calls"),
    events(
        getPrefix(frameworkInfo) + "events"),
    offers_sent(
        getPrefix(frameworkInfo) + "offers/sent"),
    offers_accepted(
        getPrefix(frameworkInfo) + "offers/accepted"),
    offers_declined(
        getPrefix(frameworkInfo) + "offers/declined"),
    offers_rescinded(
        getPrefix(frameworkInfo) + "offers/rescinded"),
    offers_with_resource_types(
        {{"cpus",
          Counter(getPrefix(frameworkInfo) +
              "offers/sent/with_cpus")},
         {"mem",
          Counter(getPrefix(frameworkInfo) +
              "offers/sent/with_mem")},
         {"disk",
          Counter(getPrefix(frameworkInfo) +
              "offers/sent/with_disk")},
         {"ports",
          Counter(getPrefix(frameworkInfo) +
              "offers/sent/with_ports")},
         {"gpus",
          Counter(getPrefix(frameworkInfo) +
              "offers/sent/with_gpus")}}),
    offered_resource_types(
        {{"cpus",
          Counter(getPrefix(frameworkInfo) +
              "offered_resources/cpus")},
         {"mem",
          Counter(getPrefix(frameworkInfo) +
              "offered_resources/mem")},
         {"disk",
          Counter(getPrefix(frameworkInfo) +
              "offered_resources/disk")},
         {"gpus",
          Counter(getPrefix(frameworkInfo) +
              "offered_resources/gpus")}}),
    operations(
        getPrefix(frameworkInfo) + "operations"),
    refuse_seconds_infinite(
        getPrefix(frameworkInfo) +
          "allocation/offer_filters/refuse_seconds/infinite"),
    refuseSecondsBuckets(
        {{Seconds(5),
          Counter(getPrefix(frameworkInfo) +
              "allocation/offer_filters/refuse_seconds/5secs")},
         {Minutes(1),
          Counter(getPrefix(frameworkInfo) +
              "allocation/offer_filters/refuse_seconds/1mins")},
         {Hours(1),
          Counter(getPrefix(frameworkInfo) +
              "allocation/offer_filters/refuse_seconds/1hours")},
         {Days(1),
          Counter(getPrefix(frameworkInfo) +
              "allocation/offer_filters/refuse_seconds/1days")}})
{
  process::metrics::add(subscribed);

  process::metrics::add(calls);
  process::metrics::add(events);
  process::metrics::add(operations);

  process::metrics::add(offers_sent);
  process::metrics::add(offers_accepted);
  process::metrics::add(offers_declined);
  process::metrics::add(offers_rescinded);

  foreachvalue (const Counter& counter, offers_with_resource_types) {
    process::metrics::add(counter);
  }

  foreachvalue (const Counter& counter, offered_resource_types) {
    process::metrics::add(counter);
  }

  process::metrics::add(refuse_seconds_infinite);

  foreachvalue (const Counter& counter, refuseSecondsBuckets) {
    process::metrics::add(counter);
  }
}


FrameworkMetrics::~FrameworkMetrics()
{
  process::metrics::remove(subscribed);

  process::metrics::remove(calls);
  foreachvalue (const Counter& counter, callTypes) {
    process::metrics::remove(counter);
  }

  process::metrics::remove(events);
  foreachvalue (const Counter& counter, eventTypes) {
    process::metrics::remove(counter);
  }

  process::metrics::remove(offers_sent);
  process::metrics::remove(offers_accepted);
  process::metrics::remove(offers_declined);
  process::metrics::remove(offers_rescinded);

  foreachvalue (const Counter& counter, offers_with_resource_types) {
    process::metrics::remove(counter);
  }

  foreachvalue (const Counter& counter, offered_resource_types) {
    process::metrics::remove(counter);
  }

  foreachvalue (const auto& source_reason, terminal_task_reasons) {
    foreachvalue (const auto& reason_counter, source_reason) {
      foreachvalue (const Counter& counter, reason_counter) {
        process::metrics::remove(counter);
      }
    }
  }

  foreachvalue (const Counter& counter, terminal_task_states) {
    process::metrics::remove(counter);
  }

  process::metrics::remove(operations);
  foreachvalue (const Counter& counter, operation_types) {
    process::metrics::remove(counter);
  }

  process::metrics::remove(refuse_seconds_infinite);
  foreachvalue (const Counter& counter, refuseSecondsBuckets) {
    process::metrics::remove(counter);
  }
}


string FrameworkMetrics::normalize(const string& s)
{
  string name = strings::lower(s);
  name = strings::trim(name);
  name = strings::replace(name, " ", "__");
  name = strings::replace(name, ".", "__");
  name = strings::replace(name, "/", "__");

  return name;
}


string FrameworkMetrics::getPrefix(const FrameworkInfo& frameworkInfo)
{
  return "master/frameworks/" + normalize(frameworkInfo.name()) +
    "." + stringify(frameworkInfo.id()) + "/";
}


void FrameworkMetrics::incrementCall(const scheduler::Call& call)
{
  if (!callTypes.contains(call.type())) {
    Counter counter = Counter(
        getPrefix(frameworkInfo) + "calls/" +
        strings::lower(scheduler::Call::Type_Name(call.type())));

    callTypes.put(call.type(), counter);
    process::metrics::add(counter);
  }

  Counter counter = callTypes.get(call.type()).get();
  counter++;
  calls++;
}


void FrameworkMetrics::incrementSubscribeCall()
{
  if (!callTypes.contains(scheduler::Call::SUBSCRIBE)) {
    Counter counter = Counter(getPrefix(frameworkInfo) + "calls/subscribe");
    callTypes.put(scheduler::Call::SUBSCRIBE, counter);
    process::metrics::add(counter);
  }

  Counter counter = callTypes.get(scheduler::Call::SUBSCRIBE).get();
  counter++;
  calls++;
}


void FrameworkMetrics::incrementEvent(const scheduler::Event& event)
{
  if (event.type() == scheduler::Event::UPDATE) {
    const TaskState& taskState = event.update().status().state();
    if (!eventUpdates.contains(taskState)) {
      Counter counter = Counter(
          getPrefix(frameworkInfo) + "events/update/" +
          strings::lower(TaskState_Name(taskState)));

      eventUpdates.put(taskState, counter);
      process::metrics::add(counter);
    }

    Counter counter = eventUpdates.get(taskState).get();
    counter++;
  }

  if (!eventTypes.contains(event.type())) {
    Counter counter = Counter(
        getPrefix(frameworkInfo) + "events/" +
        strings::lower(scheduler::Event::Type_Name(event.type())));

    eventTypes.put(event.type(), counter);
    process::metrics::add(counter);
  }

  Counter counter = eventTypes.get(event.type()).get();
  counter++;
  events++;
}


void FrameworkMetrics::incrementTerminalTaskReasons(
    const TaskState& state,
    const TaskStatus::Source& source,
    const TaskStatus::Reason& reason)
{
  if (!terminal_task_reasons.contains(state)) {
    terminal_task_reasons[state] = SourcesReasons();
  }

  if (!terminal_task_reasons[state].contains(source)) {
    terminal_task_reasons[state][source] = Reasons();
  }

  if (!terminal_task_reasons[state][source].contains(reason)) {
    Counter counter = Counter(
        getPrefix(frameworkInfo) + "tasks/" +
        strings::lower(TaskState_Name(state)) + "/" +
        strings::lower(TaskStatus::Source_Name(source)) + "/" +
        strings::lower(TaskStatus::Reason_Name(reason)));

    terminal_task_reasons[state][source].put(reason, counter);
    process::metrics::add(counter);
  }

  Counter counter = terminal_task_reasons[state][source].get(reason).get();
  counter++;
}


void FrameworkMetrics::incrementActiveTaskState(const TaskState& state)
{
  if (!active_task_states.contains(state)) {
    PushGauge gauge = PushGauge(
        getPrefix(frameworkInfo) + "tasks/" +
        strings::lower(TaskState_Name(state)));

    active_task_states.put(state, gauge);
    process::metrics::add(gauge);
  }

  active_task_states.at(state) += 1;
}


void FrameworkMetrics::decrementActiveTaskState(const TaskState& state)
{
  if (!active_task_states.contains(state)) {
    PushGauge gauge = PushGauge(
        getPrefix(frameworkInfo) + "tasks/" +
        strings::lower(TaskState_Name(state)));

    active_task_states.put(state, gauge);
    process::metrics::add(gauge);
  }

  active_task_states.at(state) -= 1;
}


void FrameworkMetrics::incrementTerminalTaskState(const TaskState& state)
{
  if (!terminal_task_states.contains(state)) {
    Counter counter = Counter(
        getPrefix(frameworkInfo) + "tasks/" +
        strings::lower(TaskState_Name(state)));

    terminal_task_states.put(state, counter);
    process::metrics::add(counter);
  }

  Counter counter = terminal_task_states.get(state).get();
  counter++;
}


void FrameworkMetrics::incrementOperation(const Offer::Operation& operation)
{
  if (!operation_types.contains(operation.type())) {
    Counter counter = Counter(
        getPrefix(frameworkInfo) + "operations/" +
        strings::lower(Offer::Operation::Type_Name(operation.type())));

    operation_types.put(operation.type(), counter);
    process::metrics::add(counter);
  }

  Counter counter = operation_types.get(operation.type()).get();
  counter++;
  operations++;
}


void FrameworkMetrics::incrementOfferFilterBuckets(const Duration _duration)
{
  refuse_seconds_infinite++;

  foreachpair (
      const Duration& duration,
      Counter& counter,
      refuseSecondsBuckets) {
    if (_duration <= duration) {
      counter++;
    }
  }
}


void FrameworkMetrics::incrementOffersWithResourceTypes(
    const Resources& resources)
{
  foreach (const Resource& resource, resources) {
    if (!Resources::isEmpty(resource)) {
      if (!offers_with_resource_types.contains(resource.name())) {
        Counter counter(
            getPrefix(frameworkInfo) +
            "offers/sent/with_" + resource.name());
        offers_with_resource_types.put(resource.name(), counter);
        process::metrics::add(counter);
        counter++;
      } else {
        offers_with_resource_types.at(resource.name())++;
      }
    }
  }
}


void FrameworkMetrics::incrementOfferedResourceTypes(const Resources& resources)
{
  foreach (const Resource& resource, resources) {
    if (resource.type() == Value::SCALAR && !Resources::isEmpty(resource)) {
      if (!offered_resource_types.contains(resource.name())) {
        Counter counter(
            getPrefix(frameworkInfo) +
            "offered_resources/" + resource.name());
        offered_resource_types.put(resource.name(), counter);
        process::metrics::add(counter);
        counter += resource.scalar().value();
      } else {
        offered_resource_types.at(resource.name()) += resource.scalar().value();
      }
    }
  }
}


string normalizeMetricKey(const string& key)
{
  string name = strings::lower(key);
  name = strings::trim(name);
  name = strings::replace(name, " ", "__");
  name = strings::replace(name, ".", "__");
  name = strings::replace(name, "/", "__");

  return name;
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
