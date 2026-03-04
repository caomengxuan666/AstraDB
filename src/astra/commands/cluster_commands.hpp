// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/commands/command_handler.hpp"
#include "astra/cluster/cluster_manager.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::commands {

// CLUSTER INFO - Get cluster information
CommandResult HandleClusterInfo(const protocol::Command& command, CommandContext* context);

// CLUSTER NODES - List all nodes in the cluster
CommandResult HandleClusterNodes(const protocol::Command& command, CommandContext* context);

// CLUSTER MEET - Add a node to the cluster
CommandResult HandleClusterMeet(const protocol::Command& command, CommandContext* context);

// CLUSTER FORGET - Remove a node from the cluster
CommandResult HandleClusterForget(const protocol::Command& command, CommandContext* context);

// CLUSTER SLOTS - Get cluster slots mapping
CommandResult HandleClusterSlots(const protocol::Command& command, CommandContext* context);

// CLUSTER REPLICAS - List replicas of a node
CommandResult HandleClusterReplicas(const protocol::Command& command, CommandContext* context);

// CLUSTER ADDSLOTS - Assign slots to a node
CommandResult HandleClusterAddSlots(const protocol::Command& command, CommandContext* context);

// CLUSTER DELSLOTS - Remove slots from a node
CommandResult HandleClusterDelSlots(const protocol::Command& command, CommandContext* context);

// CLUSTER FLUSHSLOTS - Remove all slots from a node
CommandResult HandleClusterFlushSlots(const protocol::Command& command, CommandContext* context);

// CLUSTER SETSLOT - Assign a slot to a specific node
CommandResult HandleClusterSetSlot(const protocol::Command& command, CommandContext* context);

// CLUSTER GETKEYSINSLOT - Get keys in a specific slot
CommandResult HandleClusterGetKeysInSlot(const protocol::Command& command, CommandContext* context);

// CLUSTER COUNTKEYSINSLOT - Count keys in a specific slot
CommandResult HandleClusterCountKeysInSlot(const protocol::Command& command, CommandContext* context);

// CLUSTER KEYSLOT - Calculate the hash slot for a given key
CommandResult HandleClusterKeySlot(const protocol::Command& command, CommandContext* context);

// CLUSTER BUMPEPOCH - Increment the config epoch
CommandResult HandleClusterBumpEpoch(const protocol::Command& command, CommandContext* context);

// CLUSTER RESET - Reset a node
CommandResult HandleClusterReset(const protocol::Command& command, CommandContext* context);

}  // namespace astra::commands