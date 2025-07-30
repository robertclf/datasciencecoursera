# FreeSWITCH SUBSCRIBE Handler Setup (XML + Lua Only)

This guide shows how to handle SUBSCRIBE messages during transfer scenarios using only FreeSWITCH XML configurations and Lua scripts, **without requiring any source code modifications**.

## Problem Solved

When PJSIP sends SUBSCRIBE messages during long transfers (200+ seconds), FreeSWITCH responds with 40x errors instead of meaningful "still ringing" or "transfer in progress" dialog states.

## Solution Overview

The solution uses three approaches that can be used individually or together:

1. **Lua-based Event Handler** (`subscribe_handler.lua`)
2. **XML Dialplan Configuration** (`subscribe_dialplan.xml`)  
3. **Python ESL Monitor** (`esl_subscribe_monitor.py`)

## Prerequisites

- FreeSWITCH with Lua module enabled
- SQLite3 support (usually included)
- Event Socket module enabled
- For Python approach: `python3-esl` package

## Installation Steps

### Step 1: Install Dependencies

```bash
# Install required packages (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install lua5.1 lua-sql-sqlite3 python3-esl

# For CentOS/RHEL
sudo yum install lua lua-sql-sqlite3 python3-esl
```

### Step 2: Copy Files to FreeSWITCH

```bash
# Copy Lua script to FreeSWITCH scripts directory
sudo cp subscribe_handler.lua /usr/local/freeswitch/scripts/

# Make it executable
sudo chmod +x /usr/local/freeswitch/scripts/subscribe_handler.lua

# Copy Python script (optional)
sudo cp esl_subscribe_monitor.py /usr/local/freeswitch/scripts/
sudo chmod +x /usr/local/freeswitch/scripts/esl_subscribe_monitor.py
```

### Step 3: Configure FreeSWITCH

#### A) Add to SIP Profile (internal.xml)

Add these parameters to your SIP profile (`/usr/local/freeswitch/conf/sip_profiles/internal.xml`):

```xml
<!-- Enable presence and dialog support -->
<param name="manage-presence" value="true"/>
<param name="presence-hosts" value="${domain_name}"/>
<param name="manage-shared-appearance" value="true"/>
<param name="presence-privacy" value="false"/>
<param name="send-presence-on-register" value="true"/>

<!-- Enable SIP message/info handling -->
<param name="fire-message-events" value="true"/>
<param name="fire-info-events" value="true"/>
<param name="sip-info-when-state-change" value="true"/>

<!-- Dialog support -->
<param name="enable-soa" value="true"/>
<param name="accept-blind-reg" value="true"/>
```

#### B) Update Dialplan

Add the SUBSCRIBE handling dialplan to your main dialplan (`/usr/local/freeswitch/conf/dialplan/default.xml`):

```xml
<!-- Add at the top of your default context, before other extensions -->

<!-- SUBSCRIBE Message Handler -->
<extension name="handle_subscribe" continue="false">
  <condition field="${sip_method}" expression="^SUBSCRIBE$">
    <condition field="${sip_h_Event}" expression="dialog" break="on-true">
      <!-- Log the SUBSCRIBE request -->
      <action application="log" data="INFO SUBSCRIBE received: Call-ID=${sip_call_id}, Event=${sip_h_Event}"/>
      
      <!-- Call Lua script to handle SUBSCRIBE -->
      <action application="lua" data="subscribe_handler.lua session"/>
      
      <!-- Get response from Lua script -->
      <action application="set" data="subscribe_state=${subscribe_response_state}"/>
      <action application="set" data="subscribe_xml=${subscribe_response_xml}"/>
      <action application="set" data="subscribe_expires=${subscribe_response_expires}"/>
      
      <!-- Send 200 OK response -->
      <action application="respond" data="200 OK"/>
      <action application="export" data="sip_h_Expires=${subscribe_expires}"/>
      <action application="export" data="sip_h_Content-Type=application/dialog-info+xml"/>
      
      <!-- Send NOTIFY with dialog state -->
      <action application="set" data="api_result=${sofia(notify ${sip_contact_uri} Event dialog Content-Type application/dialog-info+xml '${subscribe_xml}')}"/>
      <action application="log" data="INFO Sent NOTIFY with dialog state: ${subscribe_state}"/>
      
      <action application="hangup"/>
    </condition>
    
    <!-- Handle unsupported SUBSCRIBE events -->
    <action application="log" data="WARN Unsupported SUBSCRIBE event: ${sip_h_Event}"/>
    <action application="respond" data="489 Bad Event"/>
    <action application="hangup"/>
  </condition>
</extension>

<!-- Transfer State Tracking -->
<extension name="transfer_tracking" continue="true">
  <condition field="destination_number" expression="^(.+)$">
    <!-- Initialize transfer tracking -->
    <action application="set" data="transfer_state=none"/>
    <action application="set" data="call_start_time=${epoch_time_now}"/>
    
    <!-- Enable dialog support for this call -->
    <action application="export" data="presence_id=${caller_id_number}@${domain_name}"/>
    <action application="export" data="sip_h_Supported=dialog-info"/>
    <action application="export" data="sip_h_Allow-Events=dialog"/>
    <action application="export" data="nolocal:transfer_state=none"/>
  </condition>
</extension>
```

#### C) Enable Event Socket (if not already enabled)

Edit `/usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml`:

```xml
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <param name="listen-ip" value="127.0.0.1"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
    <param name="apply-inbound-acl" value="loopback.auto"/>
  </settings>
</configuration>
```

#### D) Configure Lua Module

Edit `/usr/local/freeswitch/conf/autoload_configs/lua.conf.xml`:

```xml
<configuration name="lua.conf" description="LUA Configuration">
  <settings>
    <!-- Auto-start the subscribe handler -->
    <param name="startup-script" value="subscribe_handler.lua listen"/>
  </settings>
</configuration>
```

### Step 4: Start the Handler

You can start the handler in multiple ways:

#### Option 1: Auto-start with FreeSWITCH (Recommended)

The Lua configuration above will auto-start the handler. To verify:

```bash
# Check FreeSWITCH CLI
fs_cli -x "console loglevel 7"
# Look for: "[SUBSCRIBE_HANDLER] Subscribe handler started"
```

#### Option 2: Manual start via CLI

```bash
fs_cli -x "luarun subscribe_handler.lua listen"
```

#### Option 3: Python ESL Alternative

```bash
# Run Python handler instead of/alongside Lua
python3 /usr/local/freeswitch/scripts/esl_subscribe_monitor.py
```

### Step 5: Testing

#### Test SUBSCRIBE Handling

1. Set up a transfer scenario with a long ring time (200+ seconds)
2. Send SUBSCRIBE messages from PJSIP during the transfer
3. Monitor FreeSWITCH logs for SUBSCRIBE handling:

```bash
# Monitor logs
tail -f /usr/local/freeswitch/log/freeswitch.log | grep SUBSCRIBE_HANDLER
```

#### Check Transfer Status

```bash
# Via Lua
fs_cli -x "lua subscribe_handler.lua status YOUR_CALL_ID"

# Via Python
python3 esl_subscribe_monitor.py status YOUR_CALL_ID
```

## How It Works

### 1. Transfer State Tracking

The system tracks transfer states in an SQLite database:
- `trying` - Transfer initiated, target not yet ringing
- `ringing` - Target is ringing (180 Ringing received)
- `answered` - Target answered (200 OK received)
- `completed` - Transfer finished successfully
- `failed` - Transfer failed

### 2. Dialog State Mapping

Transfer states are mapped to SIP dialog states:
- `trying/ringing` → `"trying"` (still in progress)
- `answered` → `"confirmed"` (call established)
- `completed/failed` → `"terminated"` (call ended)
- Transfers older than 200 seconds → `"terminated"`

### 3. SUBSCRIBE Response Flow

1. PJSIP sends SUBSCRIBE with Event: dialog
2. FreeSWITCH dialplan catches SUBSCRIBE
3. Lua script checks transfer state from database
4. Appropriate dialog-info XML is generated
5. 200 OK response sent to PJSIP
6. NOTIFY message sent with current dialog state

## Configuration Options

### Timeout Settings

Edit the timeout values in `subscribe_handler.lua`:

```lua
local config = {
    transfer_timeout = 200,  -- Consider transfer completed after this many seconds
    subscribe_expires = 3600, -- SUBSCRIBE expiration time
}
```

### Database Location

Change the database path if needed:

```lua
db_path = "/tmp/freeswitch_transfer_states.db"
```

### Logging Level

Adjust logging verbosity:

```lua
log_level = "INFO"  -- or "DEBUG" for verbose output
```

## Troubleshooting

### Common Issues

1. **SUBSCRIBE not being caught**
   - Verify SIP profile has `fire-message-events=true`
   - Check dialplan order (SUBSCRIBE handler should be first)
   - Ensure Event: dialog header is present

2. **Lua script not starting**
   - Check lua.conf.xml configuration
   - Verify script permissions and path
   - Look for Lua errors in FreeSWITCH log

3. **Database permission errors**
   - Ensure FreeSWITCH user can write to `/tmp/`
   - Change database path in script if needed

4. **NOTIFY not being sent**
   - Verify sofia profile is loaded correctly
   - Check SIP contact URI format
   - Monitor SIP traces for NOTIFY messages

### Debug Commands

```bash
# Check Lua script status
fs_cli -x "lua subscribe_handler.lua status"

# Monitor SIP messages
fs_cli -x "sofia global siptrace on"

# Check presence status
fs_cli -x "show presence"

# View active calls
fs_cli -x "show calls"
```

### Log Analysis

Look for these log entries:
- `[SUBSCRIBE_HANDLER] SUBSCRIBE received`
- `[SUBSCRIBE_HANDLER] Sent NOTIFY with state`
- `[SUBSCRIBE_HANDLER] Transfer initiated`

## Advanced Configuration

### Custom Transfer Detection

You can modify the transfer detection logic in the Lua script to match your specific transfer scenarios:

```lua
-- Custom transfer detection based on your dialplan
if event_name == "CHANNEL_BRIDGE" then
    local transfer_context = event:getHeader("variable_transfer_context")
    if transfer_context == "your_transfer_context" then
        -- Handle your specific transfer scenario
    end
end
```

### Integration with External Systems

The Python ESL script can be extended to integrate with external monitoring systems:

```python
def handle_transfer_event(self, call_id, state):
    # Send to external monitoring system
    webhook_url = "https://your-system.com/api/transfer-status"
    requests.post(webhook_url, json={'call_id': call_id, 'state': state})
```

## Performance Considerations

- The SQLite database is lightweight and suitable for moderate call volumes
- For high-volume environments, consider using PostgreSQL or MySQL
- The Event Socket connection is efficient for real-time event processing
- Lua scripts have minimal memory footprint

## Security Notes

- Event Socket password should be changed from default
- Database file should have appropriate permissions
- Consider firewall rules for Event Socket port (8021)
- SIP traces contain sensitive information - use carefully

This solution provides a robust, source-code-free approach to handling SUBSCRIBE messages during transfer scenarios in FreeSWITCH.