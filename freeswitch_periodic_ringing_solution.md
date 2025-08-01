# FreeSWITCH Periodic 180 Ringing During Transfer Solution

## Problem
Need to send "180 Ringing" messages every 30 seconds while a transfer to C-leg is in progress to prevent PJSIP from timing out.

## Solution Approaches

### 1. Using Lua Script with Scheduler (Recommended)

Create a Lua script that sends periodic ringing:

```lua
-- periodic_ringing.lua
local session = argv[1] or session
local interval = argv[2] or "30"  -- Default 30 seconds
local max_duration = argv[3] or "300"  -- Default 5 minutes max

if not session then
    freeswitch.consoleLog("ERROR", "No session provided to periodic_ringing.lua\n")
    return
end

local uuid = session:get_uuid()
local start_time = os.time()
local ring_count = 0

freeswitch.consoleLog("INFO", "Starting periodic ringing for session: " .. uuid .. " every " .. interval .. " seconds\n")

-- Function to send 180 Ringing
function send_ringing()
    if session:ready() and not session:answered() then
        session:execute("respond", "180")
        ring_count = ring_count + 1
        freeswitch.consoleLog("INFO", "Sent 180 Ringing #" .. ring_count .. " for session: " .. uuid .. "\n")
        return true
    end
    return false
end

-- Schedule periodic ringing
local function schedule_ringing()
    while session:ready() and not session:answered() do
        local elapsed = os.time() - start_time
        
        -- Stop if max duration reached
        if elapsed >= tonumber(max_duration) then
            freeswitch.consoleLog("INFO", "Max duration reached, stopping periodic ringing for: " .. uuid .. "\n")
            break
        end
        
        -- Send ringing and sleep
        if send_ringing() then
            session:sleep(tonumber(interval) * 1000)  -- Convert to milliseconds
        else
            break
        end
    end
end

-- Start the periodic ringing
schedule_ringing()
```

### 2. Enhanced Dialplan with Periodic Ringing

```xml
<context name="transfer_with_periodic_ringing">
  <extension name="outbound_transfer_with_ringing">
    <condition field="destination_number" expression="^(.+)$">
      <!-- Set variables for transfer -->
      <action application="set" data="effective_caller_id_number=${caller_id_number}"/>
      <action application="set" data="effective_caller_id_name=${caller_id_name}"/>
      
      <!-- Configure timeouts -->
      <action application="set" data="originate_timeout=300"/>
      <action application="set" data="progress_timeout=300"/>
      <action application="set" data="bridge_answer_timeout=300"/>
      
      <!-- Enable early media for progress indication -->
      <action application="set" data="bridge_early_media=true"/>
      <action application="set" data="instant_ringback=true"/>
      
      <!-- Start periodic ringing script on media -->
      <action application="set" data="api_on_media=luarun periodic_ringing.lua ${uuid} 30 300"/>
      
      <!-- Execute the bridge with custom originate -->
      <action application="bridge" data="{originate_timeout=300,progress_timeout=300}sofia/gateway/your_gateway/$1"/>
      
      <!-- Fallback if bridge fails -->
      <action application="answer"/>
      <action application="playback" data="misc/call_cannot_be_completed.wav"/>
      <action application="hangup"/>
    </condition>
  </extension>
</context>
```

### 3. Custom Module Approach (C/C++)

Create a custom FreeSWITCH module for more precise control:

```c
// mod_periodic_ringing.c
#include <switch.h>

typedef struct {
    switch_core_session_t *session;
    int interval_ms;
    int max_duration_ms;
    switch_bool_t active;
    switch_thread_t *thread;
    switch_memory_pool_t *pool;
} periodic_ringing_t;

static void *SWITCH_THREAD_FUNC periodic_ringing_thread(switch_thread_t *thread, void *obj)
{
    periodic_ringing_t *pvt = (periodic_ringing_t *) obj;
    switch_time_t start_time = switch_time_now();
    int ring_count = 0;
    
    while (pvt->active && switch_channel_ready(switch_core_session_get_channel(pvt->session))) {
        switch_channel_t *channel = switch_core_session_get_channel(pvt->session);
        
        // Check if call is answered
        if (switch_channel_test_flag(channel, CF_ANSWERED)) {
            break;
        }
        
        // Check max duration
        if (switch_time_now() - start_time > pvt->max_duration_ms * 1000) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                            "Max duration reached, stopping periodic ringing\n");
            break;
        }
        
        // Send 180 Ringing
        switch_channel_respond(channel, "180", "Ringing");
        ring_count++;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                         "Sent 180 Ringing #%d for session %s\n", 
                         ring_count, switch_core_session_get_uuid(pvt->session));
        
        // Sleep for interval
        switch_yield(pvt->interval_ms * 1000);
    }
    
    return NULL;
}

SWITCH_STANDARD_APP(periodic_ringing_start)
{
    switch_channel_t *channel = switch_core_session_get_channel(session);
    periodic_ringing_t *pvt;
    switch_memory_pool_t *pool;
    int interval = 30; // Default 30 seconds
    int max_duration = 300; // Default 5 minutes
    
    if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Memory pool creation failed\n");
        return;
    }
    
    pvt = switch_core_alloc(pool, sizeof(*pvt));
    pvt->session = session;
    pvt->pool = pool;
    pvt->interval_ms = interval;
    pvt->max_duration_ms = max_duration;
    pvt->active = SWITCH_TRUE;
    
    // Parse arguments if provided
    if (data) {
        char *argv[3] = { 0 };
        char *mydata = switch_core_session_strdup(session, data);
        int argc = switch_separate_string(mydata, ' ', argv, 3);
        
        if (argc > 0 && argv[0]) pvt->interval_ms = atoi(argv[0]);
        if (argc > 1 && argv[1]) pvt->max_duration_ms = atoi(argv[1]);
    }
    
    // Start the thread
    switch_threadattr_t *thd_attr = NULL;
    switch_threadattr_create(&thd_attr, pool);
    switch_thread_create(&pvt->thread, thd_attr, periodic_ringing_thread, pvt, pool);
    
    // Store in channel variables for cleanup
    switch_channel_set_private(channel, "periodic_ringing", pvt);
}
```

### 4. Event Socket Language (ESL) Approach

Using Python with ESL:

```python
#!/usr/bin/env python3
# periodic_ringing_esl.py

import ESL
import time
import sys
import threading

class PeriodicRinging:
    def __init__(self, uuid, interval=30, max_duration=300):
        self.uuid = uuid
        self.interval = interval
        self.max_duration = max_duration
        self.active = True
        self.start_time = time.time()
        
        # Connect to FreeSWITCH
        self.con = ESL.ESLconnection("localhost", "8021", "ClueCon")
        if not self.con.connected():
            print("Failed to connect to FreeSWITCH")
            sys.exit(1)
            
    def send_ringing(self):
        """Send 180 Ringing to the channel"""
        cmd = f"uuid_respond {self.uuid} 180"
        result = self.con.api(cmd)
        if result:
            print(f"Sent 180 Ringing to {self.uuid}: {result.getBody()}")
            return True
        return False
        
    def check_channel_status(self):
        """Check if channel is still active and not answered"""
        cmd = f"uuid_exists {self.uuid}"
        result = self.con.api(cmd)
        if not result or result.getBody().strip() != "true":
            return False
            
        # Check if answered
        cmd = f"uuid_getvar {self.uuid} channel_state"
        result = self.con.api(cmd)
        if result and "CS_EXECUTE" in result.getBody():
            return True
        return False
        
    def start_periodic_ringing(self):
        """Start sending periodic 180 Ringing messages"""
        ring_count = 0
        
        while self.active:
            elapsed = time.time() - self.start_time
            
            # Check max duration
            if elapsed >= self.max_duration:
                print(f"Max duration {self.max_duration}s reached, stopping")
                break
                
            # Check channel status
            if not self.check_channel_status():
                print("Channel no longer active or answered, stopping")
                break
                
            # Send ringing
            if self.send_ringing():
                ring_count += 1
                print(f"Sent 180 Ringing #{ring_count} to {self.uuid}")
            
            # Sleep for interval
            time.sleep(self.interval)
            
        self.cleanup()
        
    def stop(self):
        """Stop the periodic ringing"""
        self.active = False
        
    def cleanup(self):
        """Cleanup resources"""
        if self.con:
            self.con.disconnect()

def main():
    if len(sys.argv) < 2:
        print("Usage: periodic_ringing_esl.py <uuid> [interval] [max_duration]")
        sys.exit(1)
        
    uuid = sys.argv[1]
    interval = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    max_duration = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    
    ringing = PeriodicRinging(uuid, interval, max_duration)
    ringing.start_periodic_ringing()

if __name__ == "__main__":
    main()
```

### 5. Simple Scheduler-Based Approach

Using FreeSWITCH's built-in scheduler:

```xml
<extension name="transfer_with_scheduled_ringing">
  <condition field="destination_number" expression="^(.+)$">
    <!-- Set up the transfer -->
    <action application="set" data="originate_timeout=300"/>
    
    <!-- Schedule periodic ringing using sched_api -->
    <action application="sched_api" data="+30 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+60 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+90 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+120 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+150 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+180 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+210 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+240 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+270 none uuid_respond ${uuid} 180"/>
    <action application="sched_api" data="+300 none uuid_respond ${uuid} 180"/>
    
    <!-- Execute the bridge -->
    <action application="bridge" data="sofia/gateway/your_gateway/$1"/>
  </condition>
</extension>
```

### 6. Enhanced Lua Script with Better Error Handling

```lua
-- enhanced_periodic_ringing.lua
local session = argv[1] or session
local interval = tonumber(argv[2]) or 30
local max_duration = tonumber(argv[3]) or 300

if not session then
    freeswitch.consoleLog("ERROR", "No session provided\n")
    return
end

local uuid = session:get_uuid()
local channel = session:get_channel()
local start_time = os.time()
local ring_count = 0

freeswitch.consoleLog("INFO", "Starting enhanced periodic ringing for: " .. uuid .. "\n")

-- Function to check if we should continue ringing
local function should_continue()
    if not session:ready() then
        freeswitch.consoleLog("INFO", "Session not ready, stopping ringing\n")
        return false
    end
    
    if session:answered() then
        freeswitch.consoleLog("INFO", "Session answered, stopping ringing\n")
        return false
    end
    
    local elapsed = os.time() - start_time
    if elapsed >= max_duration then
        freeswitch.consoleLog("INFO", "Max duration reached, stopping ringing\n")
        return false
    end
    
    return true
end

-- Main ringing loop
while should_continue() do
    -- Send 180 Ringing
    local result = session:execute("respond", "180 Ringing")
    ring_count = ring_count + 1
    
    freeswitch.consoleLog("INFO", 
        string.format("Sent 180 Ringing #%d for %s (elapsed: %ds)", 
                     ring_count, uuid, os.time() - start_time))
    
    -- Sleep for interval (in milliseconds)
    session:sleep(interval * 1000)
end

freeswitch.consoleLog("INFO", 
    string.format("Periodic ringing stopped for %s after %d rings", 
                 uuid, ring_count))
```

## Usage Examples

### In Dialplan:
```xml
<!-- Start periodic ringing on pre_answer -->
<action application="set" data="execute_on_pre_answer=luarun enhanced_periodic_ringing.lua ${uuid} 30 300"/>

<!-- Or start when media is detected -->
<action application="set" data="api_on_media=luarun enhanced_periodic_ringing.lua ${uuid} 30 300"/>
```

### From CLI:
```bash
# Start periodic ringing for a specific call
luarun enhanced_periodic_ringing.lua <uuid> 30 300

# Using ESL
python3 periodic_ringing_esl.py <uuid> 30 300
```

## Best Practices

1. **Use the Lua script approach** for most scenarios - it's simple and effective
2. **Set reasonable intervals** - 30 seconds is usually appropriate
3. **Include max duration limits** to prevent infinite ringing
4. **Monitor session state** to stop when call is answered or terminated
5. **Log activities** for debugging and monitoring
6. **Test thoroughly** with your specific PJSIP configuration

## Implementation Recommendation

For your specific use case, I recommend using the **Enhanced Lua Script** approach with the following dialplan configuration:

```xml
<action application="set" data="api_on_media=luarun enhanced_periodic_ringing.lua ${uuid} 30 300"/>
<action application="bridge" data="{originate_timeout=300}sofia/gateway/your_gateway/$1"/>
```

This will start sending 180 Ringing every 30 seconds as soon as media is detected, and continue until the call is answered or 300 seconds have elapsed.