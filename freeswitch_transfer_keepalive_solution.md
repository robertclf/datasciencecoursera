# FreeSWITCH Transfer Keepalive Solution: Preventing PJSIP BYE Timeout During C-leg Transfer

## Problem Overview

When using FreeSWITCH as a Session Border Controller (SBC) with PJSIP as a custom application, during a transfer from B-leg to C-leg, if the C-leg takes an extended time (200 seconds) to answer, PJSIP sends a BYE message to FreeSWITCH at around 110 seconds, terminating the call prematurely.

## Root Cause Analysis

The issue is typically caused by:
1. **Session timers** - SIP session refresh timers expiring (common default: 90-120 seconds)
2. **Progress timeout** - Lack of progress indication during call setup
3. **PJSIP timeout configuration** - Default timeouts in PJSIP being too aggressive
4. **Missing keepalive mechanisms** during transfer scenarios

## Solution Approaches

### 1. Disable Session Timers (Immediate Fix)

#### For FreeSWITCH SIP Profiles
Edit your SIP profile configuration (`conf/sip_profiles/internal.xml` or `external.xml`):

```xml
<!-- Disable session timers completely -->
<param name="session-timers" value="false"/>
<param name="minimum-session-expires" value="120"/>
```

#### For PJSIP Endpoints
If using PJSIP endpoints, disable timers in the endpoint configuration:

```
[your_trunk_name](+)
timers=no
```

Add this to `pjsip.endpoint_custom_post.conf` file.

### 2. Configure Transfer Progress Timeouts

#### FreeSWITCH Transfer Variables
Set these channel variables before initiating transfers:

```xml
<!-- Extend transfer timeouts -->
<action application="set" data="progress_timeout=300"/>
<action application="set" data="bridge_answer_timeout=300"/>
<action application="set" data="originate_timeout=300"/>
<action application="set" data="call_timeout=300"/>
```

#### Transfer Progress Indication
Configure FreeSWITCH to send progress updates during transfers:

```xml
<!-- Enable early media and progress indication -->
<action application="set" data="bridge_early_media=true"/>
<action application="set" data="instant_ringback=true"/>
<action application="set" data="ringback=%(2000,4000,440,480)"/>
```

### 3. Implement Heartbeat/Keepalive Mechanisms

#### Session Heartbeat Events
Enable heartbeat events to maintain session awareness:

```xml
<!-- Enable heartbeat every 60 seconds -->
<action application="set" data="enable_heartbeat_events=60"/>
<action application="set" data="heartbeat_fire_on_set=true"/>
```

#### Custom Keepalive Script
Create a Lua script for sending periodic updates:

```lua
-- keepalive_transfer.lua
local session = ...
local uuid = session:get_uuid()

-- Function to send periodic OPTIONS keepalive
function send_keepalive()
    while session:ready() do
        freeswitch.consoleLog("INFO", "Sending keepalive for transfer session: " .. uuid)
        -- Send OPTIONS to maintain dialog
        session:execute("sip_options", "ping")
        session:sleep(30000) -- Sleep 30 seconds
    end
end

-- Start keepalive in background
session:set_tts_params("flite", "kal", "8000")
session:set_variable("transfer_keepalive", "active")
send_keepalive()
```

Usage in dialplan:
```xml
<action application="set" data="api_on_media=luarun keepalive_transfer.lua"/>
```

### 4. Enhanced Transfer Dialplan Configuration

#### Complete Transfer Context Example

```xml
<context name="transfer_with_keepalive">
  <extension name="attended_transfer_c_leg">
    <condition field="destination_number" expression="^(.+)$">
      <!-- Disable session timers for this call -->
      <action application="set" data="sip_session_timer=none"/>
      <action application="export" data="sip_session_timer=none"/>
      
      <!-- Configure extended timeouts -->
      <action application="set" data="progress_timeout=300"/>
      <action application="set" data="bridge_answer_timeout=300"/>
      <action application="set" data="originate_timeout=300"/>
      <action application="set" data="call_timeout=300"/>
      
      <!-- Enable progress indication -->
      <action application="set" data="bridge_early_media=true"/>
      <action application="set" data="instant_ringback=true"/>
      
      <!-- Set up heartbeat mechanism -->
      <action application="set" data="enable_heartbeat_events=30"/>
      
      <!-- Execute transfer with keepalive -->
      <action application="set" data="api_on_media=luarun transfer_keepalive.lua"/>
      <action application="bridge" data="{session_timeout=0}sofia/gateway/your_gateway/$1"/>
      
      <!-- Fallback handling -->
      <action application="answer"/>
      <action application="playback" data="misc/call_cannot_be_completed.wav"/>
      <action application="hangup"/>
    </condition>
  </extension>
</context>
```

### 5. SIP Profile Optimizations

#### Internal Profile Configuration
```xml
<!-- conf/sip_profiles/internal.xml -->
<param name="session-timers" value="false"/>
<param name="minimum-session-expires" value="300"/>
<param name="session-expires" value="300"/>
<param name="timer-T1" value="2000"/>
<param name="timer-T2" value="8000"/>
<param name="timer-T4" value="10000"/>
<param name="invite-timeout" value="300"/>
<param name="rtp-timeout-sec" value="300"/>
<param name="rtp-hold-timeout-sec" value="1800"/>
```

#### External Profile Configuration
```xml
<!-- conf/sip_profiles/external.xml -->
<param name="session-timers" value="false"/>
<param name="enable-timer" value="false"/>
<param name="minimum-session-expires" value="300"/>
<param name="session-expires" value="300"/>
```

### 6. PJSIP Application Configuration

#### PJSIP Timeout Settings
Configure your PJSIP application with extended timeouts:

```c
// Example PJSIP configuration
pjsip_cfg()->tsx.t1 = 2000;    // 2 seconds
pjsip_cfg()->tsx.t2 = 8000;    // 8 seconds  
pjsip_cfg()->tsx.t4 = 10000;   // 10 seconds
pjsip_cfg()->tsx.td = 300000;  // 300 seconds

// Session timer configuration
pjsip_cfg()->endpt.session_timer = PJSIP_SESS_TIMER_INACTIVE;
```

### 7. Monitoring and Debugging

#### Enable SIP Tracing
```bash
# FreeSWITCH CLI commands
sofia profile internal siptrace on
sofia profile external siptrace on

# Monitor specific calls
uuid_broadcast <uuid> sofia::siptrace_on
```

#### Log Analysis Commands
```bash
# Monitor transfer progress
fs_cli -x "show channels like transfer"

# Check session timers
fs_cli -x "sofia status profile internal"

# View active bridges
fs_cli -x "show bridged_calls"
```

### 8. Alternative Solutions

#### Call Parking Instead of Transfer
```xml
<!-- Use call parking for long-duration scenarios -->
<action application="set" data="park_timeout=300"/>
<action application="set" data="park_timeout_transfer=operator"/>
<action application="park"/>
```

#### Supervised Transfer with Progress Updates
```xml
<!-- Implement supervised transfer with periodic updates -->
<action application="set" data="transfer_on_fail=operator"/>
<action application="set" data="continue_on_fail=true"/>
<action application="set" data="hangup_after_bridge=false"/>
```

## Implementation Steps

1. **Immediate Fix**: Disable session timers in SIP profiles
2. **Configure Timeouts**: Set extended timeout values for transfers
3. **Enable Progress**: Configure early media and ringback
4. **Test Transfers**: Verify 200+ second transfers work
5. **Monitor**: Use logging to track transfer success rates
6. **Optimize**: Adjust timeouts based on real-world usage

## Testing Verification

```bash
# Test long transfer scenario
originate sofia/internal/test_number &park()
# Wait 200+ seconds before completing transfer
```

## Best Practices

1. **Session Timers**: Disable for SBC scenarios with long call setup times
2. **Progress Indication**: Always provide audio feedback during transfers
3. **Timeout Management**: Configure all timeout values consistently
4. **Monitoring**: Implement comprehensive logging for transfer scenarios
5. **Fallback Handling**: Always provide alternative paths for failed transfers

## Troubleshooting

If issues persist:
1. Check NAT/firewall UDP session timeouts
2. Verify RTP path during transfers
3. Analyze SIP message flow with packet capture
4. Review PJSIP application logs for timeout sources
5. Consider using TCP instead of UDP for SIP signaling

This solution should prevent the premature BYE message from PJSIP during long transfer scenarios while maintaining reliable call handling.