# FreeSWITCH SUBSCRIBE - Quick Reference

## Basic SUBSCRIBE Dialplan Pattern

```xml
<extension name="subscribe_handler">
  <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
    <condition field="${sip_h_Event}" expression="^presence$">
      <action application="answer"/>
      <action application="presence" data="in ${dialed_domain} ${sip_from_user}@${dialed_domain}"/>
      <action application="respond" data="200 OK"/>
    </condition>
  </condition>
</extension>
```

## Event Types Quick Reference

| Event | Purpose | Typical Use Case |
|-------|---------|------------------|
| `presence` | User status | Instant messaging, availability |
| `message-summary` | Voicemail | MWI lights, email notifications |
| `dialog` | Call state | BLF, line monitoring |
| `keep-alive` | Connection | NAT traversal, connection maintenance |

## Testing Commands

### 1. Test with SIPp
```bash
# Basic presence test
sipp -sf subscribe_test_scenario.xml -s presence 192.168.1.100

# With authentication
sipp -sf subscribe_test_scenario.xml -s presence 192.168.1.100 \
     -ap secretpassword -au testuser

# Multiple concurrent tests
sipp -sf subscribe_test_scenario.xml -s presence 192.168.1.100 -r 10 -l 100
```

### 2. Manual SIP Message
```
SUBSCRIBE sip:presence@192.168.1.100 SIP/2.0
Via: SIP/2.0/UDP 192.168.1.50:5060
From: <sip:testuser@example.com>;tag=12345
To: <sip:presence@192.168.1.100>
Call-ID: test-subscribe-001
CSeq: 1 SUBSCRIBE
Event: presence
Expires: 3600
Accept: application/pidf+xml
Content-Length: 0
```

### 3. FreeSWITCH CLI Commands
```bash
# Enable SIP trace
fs_cli -x "sofia profile internal siptrace on"

# Monitor events
fs_cli -x "events plain PRESENCE_IN PRESENCE_OUT"

# Show active presence
fs_cli -x "show presence"

# Check subscriptions
fs_cli -x "show calls"
```

## Common Variables

```xml
${sip_req_method}     <!-- SUBSCRIBE -->
${sip_h_Event}        <!-- presence, message-summary, dialog -->
${sip_from_user}      <!-- username -->
${sip_from_host}      <!-- domain -->
${sip_authorized}     <!-- true/false -->
${sip_h_Expires}      <!-- expiry time in seconds -->
${dialed_domain}      <!-- target domain -->
```

## Response Codes

| Code | Meaning | When to Use |
|------|---------|-------------|
| 200 | OK | Subscription accepted |
| 401 | Unauthorized | Authentication required |
| 400 | Bad Request | Malformed SUBSCRIBE |
| 403 | Forbidden | Not allowed to subscribe |
| 501 | Not Implemented | Event type not supported |

## Minimal Working Example

```xml
<!-- Place in /usr/local/freeswitch/conf/dialplan/public/ -->
<include>
  <context name="public">
    <extension name="simple_subscribe">
      <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
        <action application="log" data="INFO SUBSCRIBE from ${sip_from_user}"/>
        <action application="answer"/>
        <action application="respond" data="200 OK"/>
      </condition>
    </extension>
  </context>
</include>
```

## File Locations

- **Dialplan**: `/usr/local/freeswitch/conf/dialplan/public/`
- **Logs**: `/usr/local/freeswitch/log/freeswitch.log`
- **Configuration**: `/usr/local/freeswitch/conf/`

## Reload Commands

```bash
# Reload dialplan
fs_cli -x "reloadxml"

# Restart Sofia profile
fs_cli -x "sofia profile internal restart"

# Check configuration
fs_cli -x "xml_locate dialplan"
```