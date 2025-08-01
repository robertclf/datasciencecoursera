# FreeSWITCH Periodic 180 Ringing Without Early Media

## Problem
Send "180 Ringing" messages every 30 seconds during transfer to C-leg without using `bridge_early_media=true` to avoid media handling complications.

## Solution Approaches (No Early Media)

### 1. Enhanced Lua Script with Pre-Answer Hook

```lua
-- periodic_ringing_no_early_media.lua
local session = argv[1] or session
local interval = tonumber(argv[2]) or 30
local max_duration = tonumber(argv[3]) or 300

if not session then
    freeswitch.consoleLog("ERROR", "No session provided\n")
    return
end

local uuid = session:get_uuid()
local start_time = os.time()
local ring_count = 0

freeswitch.consoleLog("INFO", "Starting periodic ringing (no early media) for: " .. uuid .. "\n")

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

-- Send initial 180 Ringing immediately
session:execute("respond", "180 Ringing")
ring_count = 1
freeswitch.consoleLog("INFO", "Sent initial 180 Ringing for: " .. uuid .. "\n")

-- Main ringing loop
while should_continue() do
    -- Sleep for interval first
    session:sleep(interval * 1000)
    
    -- Check again after sleep
    if should_continue() then
        -- Send 180 Ringing
        session:execute("respond", "180 Ringing")
        ring_count = ring_count + 1
        
        freeswitch.consoleLog("INFO", 
            string.format("Sent 180 Ringing #%d for %s (elapsed: %ds)", 
                         ring_count, uuid, os.time() - start_time))
    end
end

freeswitch.consoleLog("INFO", 
    string.format("Periodic ringing stopped for %s after %d rings", 
                 uuid, ring_count))
```

### 2. Dialplan with Pre-Answer Execution (Recommended)

```xml
<context name="transfer_with_periodic_ringing_no_early_media">
  <extension name="outbound_transfer_with_ringing">
    <condition field="destination_number" expression="^(.+)$">
      <!-- Set variables for transfer -->
      <action application="set" data="effective_caller_id_number=${caller_id_number}"/>
      <action application="set" data="effective_caller_id_name=${caller_id_name}"/>
      
      <!-- Configure timeouts -->
      <action application="set" data="originate_timeout=300"/>
      <action application="set" data="progress_timeout=300"/>
      <action application="set" data="bridge_answer_timeout=300"/>
      <action application="set" data="call_timeout=300"/>
      
      <!-- Disable early media explicitly -->
      <action application="set" data="bridge_early_media=false"/>
      <action application="set" data="ignore_early_media=true"/>
      
      <!-- Start periodic ringing on pre_answer (when 18x is received) -->
      <action application="set" data="execute_on_pre_answer=luarun periodic_ringing_no_early_media.lua ${uuid} 30 300"/>
      
      <!-- Alternative: Start on ring event -->
      <action application="set" data="execute_on_ring=luarun periodic_ringing_no_early_media.lua ${uuid} 30 300"/>
      
      <!-- Execute the bridge -->
      <action application="bridge" data="{originate_timeout=300,progress_timeout=300,ignore_early_media=true}sofia/gateway/your_gateway/$1"/>
      
      <!-- Fallback if bridge fails -->
      <action application="answer"/>
      <action application="playback" data="misc/call_cannot_be_completed.wav"/>
      <action application="hangup"/>
    </condition>
  </extension>
</context>
```

### 3. Using Scheduled API Calls (Simple Approach)

```xml
<extension name="transfer_with_scheduled_ringing_no_early_media">
  <condition field="destination_number" expression="^(.+)$">
    <!-- Configure timeouts -->
    <action application="set" data="originate_timeout=300"/>
    <action application="set" data="progress_timeout=300"/>
    <action application="set" data="call_timeout=300"/>
    
    <!-- Disable early media -->
    <action application="set" data="ignore_early_media=true"/>
    
    <!-- Send immediate 180 Ringing -->
    <action application="respond" data="180 Ringing"/>
    
    <!-- Schedule periodic ringing using sched_api -->
    <action application="sched_api" data="+30 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+60 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+90 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+120 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+150 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+180 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+210 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+240 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+270 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+300 none uuid_respond ${uuid} 180 Ringing"/>
    
    <!-- Execute the bridge with early media disabled -->
    <action application="bridge" data="{ignore_early_media=true,originate_timeout=300}sofia/gateway/your_gateway/$1"/>
  </condition>
</extension>
```

### 4. Background Thread Approach (Lua)

```lua
-- background_periodic_ringing.lua
local session = argv[1] or session
local interval = tonumber(argv[2]) or 30
local max_duration = tonumber(argv[3]) or 300

if not session then
    freeswitch.consoleLog("ERROR", "No session provided\n")
    return
end

local uuid = session:get_uuid()

-- Create a background thread function
local function background_ringing()
    local start_time = os.time()
    local ring_count = 0
    
    freeswitch.consoleLog("INFO", "Starting background periodic ringing for: " .. uuid .. "\n")
    
    while true do
        -- Check if session still exists
        local session_exists = freeswitch.API():execute("uuid_exists", uuid)
        if session_exists ~= "true" then
            freeswitch.consoleLog("INFO", "Session " .. uuid .. " no longer exists, stopping ringing\n")
            break
        end
        
        -- Check if session is answered
        local channel_state = freeswitch.API():execute("uuid_getvar", uuid .. " channel_state")
        if channel_state and string.find(channel_state, "CS_EXECUTE") then
            freeswitch.consoleLog("INFO", "Session " .. uuid .. " answered, stopping ringing\n")
            break
        end
        
        -- Check max duration
        local elapsed = os.time() - start_time
        if elapsed >= max_duration then
            freeswitch.consoleLog("INFO", "Max duration reached for " .. uuid .. ", stopping ringing\n")
            break
        end
        
        -- Send 180 Ringing
        local result = freeswitch.API():execute("uuid_respond", uuid .. " 180 Ringing")
        ring_count = ring_count + 1
        
        freeswitch.consoleLog("INFO", 
            string.format("Sent background 180 Ringing #%d for %s (elapsed: %ds)", 
                         ring_count, uuid, elapsed))
        
        -- Sleep for interval
        freeswitch.msleep(interval * 1000)
    end
    
    freeswitch.consoleLog("INFO", 
        string.format("Background ringing stopped for %s after %d rings", 
                     uuid, ring_count))
end

-- Start the background thread
freeswitch.thread.create(background_ringing)
freeswitch.consoleLog("INFO", "Background ringing thread started for: " .. uuid .. "\n")
```

### 5. ESL External Script (Python)

```python
#!/usr/bin/env python3
# periodic_ringing_esl_no_early_media.py

import ESL
import time
import sys
import threading
import signal

class PeriodicRingingNoEarlyMedia:
    def __init__(self, uuid, interval=30, max_duration=300):
        self.uuid = uuid
        self.interval = interval
        self.max_duration = max_duration
        self.active = True
        self.start_time = time.time()
        self.ring_count = 0
        
        # Connect to FreeSWITCH
        self.con = ESL.ESLconnection("localhost", "8021", "ClueCon")
        if not self.con.connected():
            print("Failed to connect to FreeSWITCH")
            sys.exit(1)
            
        # Set up signal handler for graceful shutdown
        signal.signal(signal.SIGTERM, self.signal_handler)
        signal.signal(signal.SIGINT, self.signal_handler)
            
    def signal_handler(self, signum, frame):
        print(f"Received signal {signum}, stopping...")
        self.stop()
        
    def send_ringing(self):
        """Send 180 Ringing to the channel"""
        cmd = f"uuid_respond {self.uuid} 180 Ringing"
        result = self.con.api(cmd)
        if result and result.getBody().strip() == "+OK":
            self.ring_count += 1
            print(f"Sent 180 Ringing #{self.ring_count} to {self.uuid}")
            return True
        else:
            error = result.getBody() if result else "No response"
            print(f"Failed to send ringing to {self.uuid}: {error}")
            return False
        
    def check_channel_status(self):
        """Check if channel is still active and not answered"""
        # Check if channel exists
        cmd = f"uuid_exists {self.uuid}"
        result = self.con.api(cmd)
        if not result or result.getBody().strip() != "true":
            print(f"Channel {self.uuid} no longer exists")
            return False
            
        # Check if answered
        cmd = f"uuid_getvar {self.uuid} channel_state"
        result = self.con.api(cmd)
        if result:
            state = result.getBody().strip()
            if "CS_EXECUTE" in state:
                print(f"Channel {self.uuid} is answered")
                return False
        
        return True
        
    def start_periodic_ringing(self):
        """Start sending periodic 180 Ringing messages"""
        print(f"Starting periodic ringing for {self.uuid} every {self.interval}s")
        
        # Send initial ringing
        self.send_ringing()
        
        while self.active:
            time.sleep(self.interval)
            
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
            if not self.send_ringing():
                break
            
        self.cleanup()
        
    def stop(self):
        """Stop the periodic ringing"""
        self.active = False
        
    def cleanup(self):
        """Cleanup resources"""
        if self.con:
            self.con.disconnect()
        print(f"Periodic ringing stopped for {self.uuid} after {self.ring_count} rings")

def main():
    if len(sys.argv) < 2:
        print("Usage: periodic_ringing_esl_no_early_media.py <uuid> [interval] [max_duration]")
        sys.exit(1)
        
    uuid = sys.argv[1]
    interval = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    max_duration = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    
    ringing = PeriodicRingingNoEarlyMedia(uuid, interval, max_duration)
    ringing.start_periodic_ringing()

if __name__ == "__main__":
    main()
```

### 6. Complete Dialplan Example

```xml
<context name="complete_transfer_no_early_media">
  <extension name="transfer_c_leg">
    <condition field="destination_number" expression="^9(.+)$">
      <!-- Log the transfer attempt -->
      <action application="log" data="INFO Starting transfer to C-leg: $1"/>
      
      <!-- Set caller ID for outbound -->
      <action application="set" data="effective_caller_id_number=${caller_id_number}"/>
      <action application="set" data="effective_caller_id_name=${caller_id_name}"/>
      
      <!-- Configure all timeouts -->
      <action application="set" data="originate_timeout=300"/>
      <action application="set" data="progress_timeout=300"/>
      <action application="set" data="bridge_answer_timeout=300"/>
      <action application="set" data="call_timeout=300"/>
      
      <!-- Explicitly disable early media -->
      <action application="set" data="bridge_early_media=false"/>
      <action application="set" data="ignore_early_media=true"/>
      <action application="set" data="instant_ringback=false"/>
      
      <!-- Send immediate 180 Ringing -->
      <action application="respond" data="180 Ringing"/>
      
      <!-- Set up periodic ringing on pre_answer -->
      <action application="set" data="execute_on_pre_answer=luarun periodic_ringing_no_early_media.lua ${uuid} 30 300"/>
      
      <!-- Alternative scheduled approach -->
      <!-- 
      <action application="sched_api" data="+30 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+60 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+90 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+120 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+150 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+180 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+210 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+240 none uuid_respond ${uuid} 180 Ringing"/>
      <action application="sched_api" data="+270 none uuid_respond ${uuid} 180 Ringing"/>
      -->
      
      <!-- Execute the bridge with no early media -->
      <action application="bridge" data="{ignore_early_media=true,originate_timeout=300,progress_timeout=300}sofia/gateway/your_gateway/$1"/>
      
      <!-- Fallback handling -->
      <action application="log" data="WARNING Transfer to $1 failed: ${bridge_hangup_cause}"/>
      <action application="answer"/>
      <action application="playback" data="misc/call_cannot_be_completed.wav"/>
      <action application="hangup"/>
    </condition>
  </extension>
</context>
```

## Usage Examples

### Start Periodic Ringing in Dialplan:
```xml
<!-- On pre-answer (when any 18x response is received) -->
<action application="set" data="execute_on_pre_answer=luarun periodic_ringing_no_early_media.lua ${uuid} 30 300"/>

<!-- On ring detection -->
<action application="set" data="execute_on_ring=luarun periodic_ringing_no_early_media.lua ${uuid} 30 300"/>
```

### Manual CLI Commands:
```bash
# Start periodic ringing manually
luarun periodic_ringing_no_early_media.lua <uuid> 30 300

# Using external ESL script
python3 periodic_ringing_esl_no_early_media.py <uuid> 30 300

# Send single 180 Ringing
uuid_respond <uuid> 180 Ringing
```

## Best Practices (No Early Media)

1. **Use `ignore_early_media=true`** in bridge parameters
2. **Set `bridge_early_media=false`** explicitly  
3. **Send immediate 180 Ringing** before starting periodic messages
4. **Use `execute_on_pre_answer`** to start ringing when first 18x is received
5. **Monitor session state** to stop ringing when answered
6. **Include timeout safeguards** to prevent infinite ringing

## Recommended Implementation

For your use case, I recommend the **Scheduled API approach** as it's simple and doesn't require early media:

```xml
<extension name="recommended_transfer">
  <condition field="destination_number" expression="^(.+)$">
    <action application="set" data="originate_timeout=300"/>
    <action application="set" data="ignore_early_media=true"/>
    
    <!-- Send immediate ringing -->
    <action application="respond" data="180 Ringing"/>
    
    <!-- Schedule periodic ringing -->
    <action application="sched_api" data="+30 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+60 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+90 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+120 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+150 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+180 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+210 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+240 none uuid_respond ${uuid} 180 Ringing"/>
    <action application="sched_api" data="+270 none uuid_respond ${uuid} 180 Ringing"/>
    
    <action application="bridge" data="{ignore_early_media=true,originate_timeout=300}sofia/gateway/your_gateway/$1"/>
  </condition>
</extension>
```

This approach avoids early media complications while ensuring PJSIP receives regular 180 Ringing messages to prevent the 110-second timeout.