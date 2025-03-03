/**
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LIBMINIFI_INCLUDE_CORE_PROCESSGROUP_H_
#define LIBMINIFI_INCLUDE_CORE_PROCESSGROUP_H_

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <set>

#include "Processor.h"
#include "Exception.h"
#include "TimerDrivenSchedulingAgent.h"
#include "EventDrivenSchedulingAgent.h"
#include "CronDrivenSchedulingAgent.h"
#include "core/logging/Logger.h"
#include "controller/ControllerServiceNode.h"
#include "controller/ControllerServiceMap.h"
#include "utils/Id.h"
#include "utils/HTTPClient.h"
#include "utils/CallBackTimer.h"

struct ProcessGroupTestAccessor;

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace core {

// Process Group Type
enum ProcessGroupType {
  ROOT_PROCESS_GROUP = 0,
  SIMPLE_PROCESS_GROUP,
  REMOTE_PROCESS_GROUP,
  MAX_PROCESS_GROUP_TYPE
};

#define ONSCHEDULE_RETRY_INTERVAL 30000  // millisecs

// ProcessGroup Class
class ProcessGroup : public CoreComponent {
  friend struct ::ProcessGroupTestAccessor;
 public:
  enum class Traverse {
    ExcludeChildren,
    IncludeChildren
  };
  // Constructor
  /*!
   * Create a new process group
   */

  ProcessGroup(ProcessGroupType type, const std::string& name, const utils::Identifier& uuid, int version, ProcessGroup *parent);
  ProcessGroup(ProcessGroupType type, const std::string& name);
  ProcessGroup(ProcessGroupType type, const std::string& name, const utils::Identifier& uuid);
  ProcessGroup(ProcessGroupType type, const std::string& name, const utils::Identifier& uuid, int version);
  // Destructor
  virtual ~ProcessGroup();
  // Set URL
  void setURL(std::string url) {
    url_ = url;
  }
  // Get URL
  std::string getURL(void) {
    return (url_);
  }
  // SetTransmitting
  void setTransmitting(bool val) {
    transmitting_ = val;
  }
  // Get Transmitting
  bool getTransmitting() {
    return transmitting_;
  }
  // setTimeOut
  void setTimeOut(uint64_t time) {
    timeOut_ = time;
  }
  uint64_t getTimeOut() {
    return timeOut_;
  }
  // setInterface
  void setInterface(std::string &ifc) {
    local_network_interface_ = ifc;
  }
  std::string getInterface() {
    return local_network_interface_;
  }
  void setTransportProtocol(std::string &protocol) {
    transport_protocol_ = protocol;
  }
  std::string getTransportProtocol() {
    return transport_protocol_;
  }
  void setHttpProxyHost(std::string &host) {
    proxy_.host = host;
  }
  std::string getHttpProxyHost() {
    return proxy_.host;
  }
  void setHttpProxyUserName(std::string &username) {
    proxy_.username = username;
  }
  std::string getHttpProxyUserName() {
    return proxy_.username;
  }
  void setHttpProxyPassWord(std::string &password) {
    proxy_.password = password;
  }
  std::string getHttpProxyPassWord() {
    return proxy_.password;
  }
  void setHttpProxyPort(int port) {
    proxy_.port = port;
  }
  int getHttpProxyPort() {
    return proxy_.port;
  }
  utils::HTTPProxy getHTTPProxy() {
    return proxy_;
  }
  // Set Processor yield period in MilliSecond
  void setYieldPeriodMsec(uint64_t period) {
    yield_period_msec_ = period;
  }
  // Get Processor yield period in MilliSecond
  uint64_t getYieldPeriodMsec(void) {
    return (yield_period_msec_);
  }

  void setOnScheduleRetryPeriod(int64_t period) {
    onschedule_retry_msec_ = period;
  }

  int64_t getOnScheduleRetryPeriod() {
    return onschedule_retry_msec_;
  }

  // getVersion
  int getVersion() {
    return config_version_;
  }
  // Start Processing
  void startProcessing(const std::shared_ptr<TimerDrivenSchedulingAgent>& timeScheduler, const std::shared_ptr<EventDrivenSchedulingAgent> &eventScheduler, const std::shared_ptr<CronDrivenSchedulingAgent> &cronScheduler); // NOLINT
  // Stop Processing
  void stopProcessing(const std::shared_ptr<TimerDrivenSchedulingAgent>& timeScheduler, const std::shared_ptr<EventDrivenSchedulingAgent> &eventScheduler, const std::shared_ptr<CronDrivenSchedulingAgent> &cronScheduler, const std::function<bool(const std::shared_ptr<Processor>&)>& filter = [] (const std::shared_ptr<Processor>&) {return true;}); // NOLINT
  // Whether it is root process group
  bool isRootProcessGroup();

  bool isRemoteProcessGroup();
  // set parent process group
  void setParent(ProcessGroup *parent) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    parent_process_group_ = parent;
  }
  // get parent process group
  ProcessGroup *getParent(void) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return parent_process_group_;
  }
  // Add processor
  void addProcessor(const std::shared_ptr<Processor>& processor);
  // Remove processor
  void removeProcessor(const std::shared_ptr<Processor>& processor);
  // Add child processor group
  void addProcessGroup(std::unique_ptr<ProcessGroup> child);
  // ! Add connections
  void addConnection(const std::shared_ptr<Connection>& connection);
  // Generic find
  template <typename Fun>
  std::shared_ptr<Processor> findProcessor(Fun condition, Traverse traverse) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto found = std::find_if(processors_.cbegin(), processors_.cend(), condition);
    if (found != processors_.cend()) {
      return *found;
    }
    for (const auto& processGroup : child_process_groups_) {
      if (processGroup->isRemoteProcessGroup() || traverse == Traverse::IncludeChildren) {
        std::shared_ptr<Processor> processor = processGroup->findProcessor(condition, traverse);
        if (processor) {
          return processor;
        }
      }
    }
    return nullptr;
  }
  // findProcessor based on UUID
  std::shared_ptr<Processor> findProcessorById(const utils::Identifier& uuid, Traverse traverse = Traverse::IncludeChildren) const;
  // findProcessor based on name
  std::shared_ptr<Processor> findProcessorByName(const std::string &processorName, Traverse traverse = Traverse::IncludeChildren) const;

  void getAllProcessors(std::vector<std::shared_ptr<Processor>> &processor_vec);
  /**
   * Add controller service
   * @param nodeId node identifier
   * @param node controller service node.
   */
  void addControllerService(const std::string &nodeId, const std::shared_ptr<core::controller::ControllerServiceNode> &node);

  /**
   * Find controllerservice node will search child groups until the nodeId is found.
   * @param node node identifier
   * @return controller service node, if it exists.
   */
  std::shared_ptr<core::controller::ControllerServiceNode> findControllerService(const std::string &nodeId);

  // removeConnection
  void removeConnection(const std::shared_ptr<Connection>& connection);
  // update property value
  void updatePropertyValue(std::string processorName, std::string propertyName, std::string propertyValue);

  void getConnections(std::map<std::string, std::shared_ptr<Connection>> &connectionMap);

  void getConnections(std::map<std::string, std::shared_ptr<Connectable>> &connectionMap);

  void getFlowFileContainers(std::map<std::string, std::shared_ptr<Connectable>> &containers) const;

  void drainConnections();

  std::size_t getTotalFlowFileCount() const;

 protected:
  void startProcessingProcessors(const std::shared_ptr<TimerDrivenSchedulingAgent>& timeScheduler, const std::shared_ptr<EventDrivenSchedulingAgent> &eventScheduler, const std::shared_ptr<CronDrivenSchedulingAgent> &cronScheduler); // NOLINT

  // version
  int config_version_;
  // Process Group Type
  const ProcessGroupType type_;
  // Processors (ProcessNode) inside this process group which include Input/Output Port, Remote Process Group input/Output port
  std::set<std::shared_ptr<Processor>> processors_;
  std::set<std::shared_ptr<Processor>> failed_processors_;
  std::set<std::unique_ptr<ProcessGroup>> child_process_groups_;
  // Connections between the processor inside the group;
  std::set<std::shared_ptr<Connection>> connections_;
  // Parent Process Group
  ProcessGroup* parent_process_group_;
  // Yield Period in Milliseconds
  std::atomic<uint64_t> yield_period_msec_;
  std::atomic<uint64_t> timeOut_;
  std::atomic<int64_t> onschedule_retry_msec_;

  // URL
  std::string url_;
  // local network interface
  std::string local_network_interface_;
  // Transmitting
  std::atomic<bool> transmitting_;
  // http proxy
  utils::HTTPProxy proxy_;
  std::string transport_protocol_;

  // controller services

  core::controller::ControllerServiceMap controller_service_map_;

 private:
  // Mutex for protection
  mutable std::recursive_mutex mutex_;
  // Logger
  std::shared_ptr<logging::Logger> logger_;
  // Prevent default copy constructor and assignment operation
  // Only support pass by reference or pointer
  ProcessGroup(const ProcessGroup &parent);
  ProcessGroup &operator=(const ProcessGroup &parent);
  static std::shared_ptr<utils::IdGenerator> id_generator_;
  std::unique_ptr<utils::CallBackTimer> onScheduleTimer_;
};
}  // namespace core
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org
#endif  // LIBMINIFI_INCLUDE_CORE_PROCESSGROUP_H_
