# FreeSWITCH SUBSCRIBE Message Handling Examples

This document provides comprehensive examples for handling SIP SUBSCRIBE messages in FreeSWITCH, along with test scenarios and implementation details.

## Overview

The provided dialplan examples handle four main types of SUBSCRIBE messages:

1. **Presence Subscriptions** - For buddy list and presence status
2. **Message Waiting Indicator (MWI)** - For voicemail notifications
3. **Dialog-Info Subscriptions** - For call state monitoring
4. **Keep-Alive Subscriptions** - For connection maintenance

## File Structure

```
freeswitch_subscribe_dialplan.xml    # Main dialplan with SUBSCRIBE handlers
sip_profile_subscribe_config.xml     # SIP profile configuration
```

## Implementation Details

### 1. Presence Subscriptions

The dialplan handles presence subscriptions by:
- Logging the SUBSCRIBE request details
- Setting up presence tracking
- Responding with 200 OK
- Sending initial presence notification

**SIP Flow Example:**
```
SUBSCRIBE sip:1001@example.com SIP/2.0
Event: presence
From: <sip:1002@example.com>
To: <sip:1001@example.com>
Contact: <sip:1002@192.168.1.100:5060>
Expires: 3600
```

**FreeSWITCH Response:**
```
SIP/2.0 200 OK
Expires: 3600
Contact: <sip:1001@example.com>
```

### 2. Message Waiting Indicator (MWI)

Handles voicemail notification subscriptions:
- Checks current voicemail count
- Responds with subscription acceptance
- Sends MWI NOTIFY with message counts

**SIP Flow Example:**
```
SUBSCRIBE sip:1001@example.com SIP/2.0
Event: message-summary
From: <sip:1001@example.com>
To: <sip:1001@example.com>
Expires: 3600
```

**MWI NOTIFY Response:**
```
NOTIFY sip:1001@192.168.1.100:5060 SIP/2.0
Event: message-summary
Subscription-State: active;expires=3599
Content-Type: application/simple-message-summary

Messages-Waiting: yes
Message-Account: sip:1001@example.com
Voice-Message: 2/5 (2/3)
```

### 3. Dialog-Info Subscriptions

Monitors call state information:
- Tracks active calls and dialog states
- Provides call pickup information
- Sends dialog state updates

**Dialog-Info NOTIFY Example:**
```xml
<?xml version="1.0"?>
<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" version="1" state="full" entity="sip:1001@example.com">
  <dialog id="call-id-12345" call-id="call-id-12345" direction="initiator">
    <state>early</state>
    <remote>
      <identity>sip:1002@example.com</identity>
      <target uri="sip:1002@example.com"/>
    </remote>
    <local>
      <identity>sip:1001@example.com</identity>
      <target uri="sip:1001@example.com"/>
    </local>
  </dialog>
</dialog-info>
```

## Testing SUBSCRIBE Functionality

### Using SIPp for Testing

**1. Test Presence Subscription:**
```bash
sipp -sf presence_subscribe.xml 192.168.1.100:5060 -m 1
```

**2. Test MWI Subscription:**
```bash
sipp -sf mwi_subscribe.xml 192.168.1.100:5060 -m 1
```

### Using fs_cli for Debugging

Monitor SUBSCRIBE messages in FreeSWITCH:
```bash
fs_cli -x "sofia loglevel all 9"
fs_cli -x "console loglevel debug"
```

### Manual SIP Testing

**Send a presence SUBSCRIBE:**
```
SUBSCRIBE sip:1001@192.168.1.100 SIP/2.0
Via: SIP/2.0/UDP 192.168.1.101:5060;branch=z9hG4bK-test
Max-Forwards: 70
From: "Test User" <sip:test@192.168.1.101>;tag=test123
To: <sip:1001@192.168.1.100>
Call-ID: test-call-id-12345
CSeq: 1 SUBSCRIBE
Event: presence
Expires: 3600
Accept: application/pidf+xml
Contact: <sip:test@192.168.1.101:5060>
Content-Length: 0
```

## Configuration Tips

### 1. Enable Presence Module
```xml
<!-- In conf/autoload_configs/modules.conf.xml -->
<load module="mod_presence"/>
```

### 2. Configure Event Socket (Optional)
For external presence management:
```xml
<!-- In conf/autoload_configs/event_socket.conf.xml -->
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <param name="listen-ip" value="127.0.0.1"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
  </settings>
</configuration>
```

### 3. Database Integration
For persistent presence and MWI:
```xml
<!-- In conf/autoload_configs/switch.conf.xml -->
<param name="core-db-dsn" value="sqlite://core"/>
```

## Common Issues and Solutions

### Issue 1: SUBSCRIBE Returns 481 Call/Transaction Does Not Exist
**Solution:** Ensure the SIP profile has `manage-presence` set to `true`

### Issue 2: MWI Notifications Not Sent
**Solutions:**
- Verify voicemail module is loaded
- Check MWI subscription in database
- Ensure correct domain matching

### Issue 3: Presence Not Working
**Solutions:**
- Enable presence module in modules.conf.xml
- Set `presence-hosts` parameter in SIP profile
- Check presence database tables

## Security Considerations

1. **Authentication:** Always authenticate SUBSCRIBE requests for internal users
2. **Authorization:** Prevent users from subscribing to unauthorized resources
3. **Rate Limiting:** Implement rate limiting for SUBSCRIBE requests
4. **Logging:** Log all SUBSCRIBE attempts for security monitoring

## Advanced Features

### Custom Presence States
```xml
<action application="presence" data="in user@domain available 'Custom Status'"/>
```

### Conditional MWI
```xml
<condition field="${user_exists(id $1 $${domain})}" expression="true">
  <!-- Handle MWI for existing users only -->
</condition>
```

### Event-Based Notifications
```xml
<action application="event" data="Event-Name=CUSTOM,Event-Subclass=presence::subscribe,User=$1,Subscriber=${sip_from_user}"/>
```

## Integration with External Systems

### REST API Integration
Use `mod_xml_curl` to fetch presence/MWI data from external systems:
```xml
<action application="curl" data="http://api.example.com/presence/$1 get"/>
```

### Database Integration
Query user presence from database:
```xml
<action application="set" data="presence_status=${db(select status from presence where user='$1')}"/>
```

This comprehensive example provides a solid foundation for implementing SUBSCRIBE message handling in FreeSWITCH with proper security, logging, and error handling.