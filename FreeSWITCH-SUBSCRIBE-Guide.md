# FreeSWITCH SUBSCRIBE Message Handling - Dialplan Examples

## Overview

This guide provides comprehensive FreeSWITCH dialplan examples for handling SIP SUBSCRIBE messages. SUBSCRIBE is a SIP method used for requesting notification of events, commonly used for:

- **Presence information** - User availability status
- **Message Waiting Indication (MWI)** - Voicemail notifications
- **Dialog state** - Call state monitoring
- **Keep-alive** - Subscription maintenance

## Key Components

### 1. Presence SUBSCRIBE Handler

```xml
<extension name="presence_subscribe" continue="false">
  <condition field="destination_number" expression="^(presence)$">
    <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
      <condition field="${sip_h_Event}" expression="^presence$">
        <!-- Handle presence subscription -->
      </condition>
    </condition>
  </condition>
</extension>
```

**Purpose**: Handles presence subscriptions for monitoring user availability status.

**Key Features**:
- Validates the Event header for "presence"
- Sets presence state using `presence` application
- Responds with 200 OK
- Logs subscription details

### 2. Message Waiting Indication (MWI) Handler

```xml
<extension name="mwi_subscribe" continue="false">
  <condition field="destination_number" expression="^(mwi|voicemail)$">
    <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
      <condition field="${sip_h_Event}" expression="^message-summary$">
        <!-- Handle MWI subscription -->
      </condition>
    </condition>
  </condition>
</extension>
```

**Purpose**: Manages voicemail notification subscriptions.

**Key Features**:
- Checks for "message-summary" event type
- Uses `vm_check` to verify voicemail status
- Automatically sends NOTIFY messages when voicemail status changes

### 3. Dialog State Handler

```xml
<extension name="dialog_subscribe" continue="false">
  <condition field="destination_number" expression="^(dialog)$">
    <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
      <condition field="${sip_h_Event}" expression="^dialog$">
        <!-- Handle dialog state subscription -->
      </condition>
    </condition>
  </condition>
</extension>
```

**Purpose**: Monitors call dialog states for line appearance monitoring.

### 4. Authentication Handler

```xml
<extension name="authenticated_subscribe" continue="false">
  <condition field="${sip_req_method}" expression="^SUBSCRIBE$">
    <condition field="${sip_authorized}" expression="^true$">
      <!-- Process authenticated subscription -->
    </condition>
    <condition field="${sip_authorized}" expression="^false$">
      <!-- Reject unauthenticated subscription -->
    </condition>
  </condition>
</extension>
```

**Purpose**: Ensures only authorized users can create subscriptions.

**Security Features**:
- Validates SIP authentication
- Sets subscription expiry time
- Rejects unauthorized requests with 401 response

## Important Variables

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `${sip_req_method}` | SIP request method | "SUBSCRIBE" |
| `${sip_h_Event}` | Event header value | "presence", "message-summary", "dialog" |
| `${sip_from_user}` | Username from From header | "user1001" |
| `${sip_from_host}` | Domain from From header | "example.com" |
| `${sip_authorized}` | Authentication status | "true" or "false" |
| `${sip_h_Expires}` | Subscription expiry time | "3600" |
| `${dialed_domain}` | Target domain | "example.com" |

## Installation Instructions

### 1. Add to FreeSWITCH Configuration

Place the dialplan example in your FreeSWITCH configuration:

```bash
# Copy to FreeSWITCH conf directory
cp freeswitch-subscribe-example.xml /usr/local/freeswitch/conf/dialplan/public/
```

### 2. Include in Main Dialplan

Add to your main `public.xml` dialplan:

```xml
<include>
  <context name="public">
    <!-- Other extensions -->
    <X-PRE-PROCESS cmd="include" data="freeswitch-subscribe-example.xml"/>
  </context>
</include>
```

### 3. Reload Configuration

```bash
# Reload FreeSWITCH configuration
fs_cli -x "reloadxml"
```

## Testing SUBSCRIBE Requests

### Using SIPp

```bash
# Test presence subscription
sipp -sf subscribe_scenario.xml -s presence your.freeswitch.server
```

### Using curl (for testing)

```bash
# Example SUBSCRIBE request
curl -X SUBSCRIBE \
  -H "Event: presence" \
  -H "Expires: 3600" \
  -H "Accept: application/pidf+xml" \
  http://your.freeswitch.server:5060/presence
```

### Manual SIP Testing

```
SUBSCRIBE sip:presence@your.domain.com SIP/2.0
Via: SIP/2.0/UDP client.domain.com:5060
From: <sip:user@domain.com>
To: <sip:presence@your.domain.com>
Call-ID: 12345@client.domain.com
CSeq: 1 SUBSCRIBE
Event: presence
Expires: 3600
Accept: application/pidf+xml
Content-Length: 0
```

## Common Event Types

| Event Type | Purpose | Content-Type |
|------------|---------|--------------|
| `presence` | User availability status | `application/pidf+xml` |
| `message-summary` | Voicemail notifications | `application/simple-message-summary` |
| `dialog` | Call state monitoring | `application/dialog-info+xml` |
| `keep-alive` | Subscription maintenance | `text/plain` |

## Best Practices

### 1. Authentication
- Always validate authentication for SUBSCRIBE requests
- Use strong authentication mechanisms
- Implement rate limiting for subscription requests

### 2. Logging
- Log all SUBSCRIBE requests with detailed information
- Monitor for unusual subscription patterns
- Track subscription success/failure rates

### 3. Resource Management
- Set reasonable expiry times (typically 3600 seconds)
- Clean up expired subscriptions
- Limit concurrent subscriptions per user

### 4. Error Handling
- Provide meaningful error responses
- Handle malformed requests gracefully
- Implement fallback mechanisms

## Troubleshooting

### Common Issues

1. **401 Unauthorized**
   - Check SIP authentication credentials
   - Verify user registration status
   - Review authentication realm settings

2. **400 Bad Request**
   - Validate Event header presence
   - Check SIP message formatting
   - Verify required headers

3. **Subscription Not Working**
   - Check dialplan execution order
   - Verify extension matching patterns
   - Review FreeSWITCH logs

### Debug Commands

```bash
# Enable SIP debugging
fs_cli -x "sofia profile internal siptrace on"

# Monitor presence events
fs_cli -x "events plain all"

# Check active subscriptions
fs_cli -x "show calls"
```

## Advanced Configuration

### Custom Event Handlers

You can extend the dialplan to handle custom events:

```xml
<extension name="custom_event_subscribe">
  <condition field="${sip_h_Event}" expression="^custom-event$">
    <!-- Custom event handling logic -->
  </condition>
</extension>
```

### Integration with External Systems

```xml
<action application="lua" data="handle_subscribe.lua ${sip_h_Event} ${sip_from_user}"/>
```

### Load Balancing

For high-traffic environments, consider:
- Distributing subscriptions across multiple FreeSWITCH instances
- Using database storage for subscription state
- Implementing subscription clustering

## Security Considerations

1. **Rate Limiting**: Implement SUBSCRIBE request rate limiting
2. **Access Control**: Restrict subscription access by IP/domain
3. **Encryption**: Use TLS for SIP signaling
4. **Monitoring**: Monitor for subscription abuse patterns

This comprehensive example provides a solid foundation for handling SUBSCRIBE messages in FreeSWITCH while maintaining security and reliability.