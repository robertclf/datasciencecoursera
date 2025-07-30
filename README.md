# FreeSWITCH SUBSCRIBE Handler for Transfer Scenarios

This project provides a comprehensive solution for handling SUBSCRIBE messages during transfer scenarios when using FreeSWITCH as an SBC with PJSIP as a custom application. Instead of returning 40x responses when PJSIP sends SUBSCRIBE messages during long transfers (e.g., 200 seconds), FreeSWITCH will respond with appropriate "still ringing" or "transfer in progress" dialog states.

## Problem Description

When using FreeSWITCH as an SBC and PJSIP as a custom application:
1. A transfer from b-leg to c-leg is initiated
2. The c-leg takes 200 seconds to pick up the phone
3. At around 60 seconds, PJSIP sends a SUBSCRIBE message to FreeSWITCH
4. FreeSWITCH responds with a 40x error instead of meaningful dialog state information

## Solution Overview

This solution provides:
- **Custom FreeSWITCH module** (`mod_subscribe_handler`) that tracks transfer states
- **Sofia SIP patch** for enhanced SUBSCRIBE handling during transfers
- **Dialplan configurations** for transfer state tracking
- **Dialog state notifications** with appropriate XML content

## Components

### 1. FreeSWITCH Subscribe Handler Module (`freeswitch_subscribe_handler.c`)

A custom FreeSWITCH module that:
- Tracks transfer states (trying, ringing, answered, completed, failed)
- Handles SUBSCRIBE messages for dialog events
- Responds with appropriate dialog-info XML based on transfer state and timing
- Provides API commands for monitoring transfer status

**Key Features:**
- Thread-safe transfer context management
- Time-based state transitions (200-second timeout handling)
- Comprehensive dialog-info XML generation
- Event-driven state updates

### 2. Sofia SIP Patch (`sofia_sip_subscribe_patch.c`)

Modifications to mod_sofia for enhanced SUBSCRIBE handling:
- Intercepts SUBSCRIBE messages for dialog events
- Checks for active transfer scenarios
- Generates appropriate dialog-info XML responses
- Handles timing constraints for long transfers

### 3. Dialplan Configuration (`freeswitch_transfer_dialplan.xml`)

XML dialplan configurations that:
- Initialize transfer state tracking variables
- Handle attended and blind transfers
- Track transfer progress through various stages
- Export necessary variables for SUBSCRIBE handlers

### 4. Build System (`Makefile`)

Complete build and installation system:
- Compiles the custom module
- Installs configurations
- Provides development and testing targets

## Installation

### Prerequisites

- FreeSWITCH development environment
- GCC compiler
- FreeSWITCH source code (for headers)

### Build and Install

1. **Clone or download the files:**
   ```bash
   # Ensure you have all the files in the same directory
   ls -la
   # Should show: freeswitch_subscribe_handler.c, sofia_sip_subscribe_patch.c, 
   #              freeswitch_transfer_dialplan.xml, Makefile, README.md
   ```

2. **Configure build paths:**
   ```bash
   # Edit Makefile if your FreeSWITCH is installed elsewhere
   vim Makefile
   # Modify FREESWITCH_DIR and FREESWITCH_SRC as needed
   ```

3. **Build and install:**
   ```bash
   # Build the module
   make all
   
   # Install everything
   make dev-install
   ```

4. **Load the module in FreeSWITCH:**
   ```bash
   # Add to conf/autoload_configs/modules.conf.xml
   <load module="mod_subscribe_handler"/>
   
   # Or load dynamically
   fs_cli -x "load mod_subscribe_handler"
   ```

### Sofia SIP Integration

The Sofia SIP patch needs to be manually integrated:

1. **Locate FreeSWITCH source:**
   ```bash
   cd /usr/src/freeswitch/src/mod/endpoints/mod_sofia/
   ```

2. **Apply the patch:**
   ```bash
   # Backup original
   cp sofia.c sofia.c.backup
   
   # Integrate the patch code from sofia_sip_subscribe_patch.c
   # Add the functions and modify the nua_i_subscribe case
   ```

3. **Rebuild mod_sofia:**
   ```bash
   make mod_sofia-install
   ```

## Configuration

### Dialog State Responses

The module responds with different dialog states based on transfer progress:

- **`trying`**: Transfer initiated, target not yet ringing
- **`early`**: Target is ringing (first 200 seconds)
- **`confirmed`**: Target answered the call
- **`terminated`**: Transfer completed, failed, or timed out

### Transfer State Variables

The dialplan sets these variables for tracking:

- `transfer_state`: Current transfer state (trying, ringing, answered, completed, failed)
- `transfer_start_time`: Unix timestamp when transfer started
- `transfer_target`: Target extension/number for transfer
- `transfer_type`: Type of transfer (attended, blind)

## Usage

### Basic Transfer with State Tracking

```xml
<!-- Attended transfer -->
<extension name="attended_transfer">
  <condition field="destination_number" expression="^(\*87)(.+)$">
    <action application="set" data="transfer_state=trying"/>
    <action application="set" data="transfer_start_time=${switch_time_now()}"/>
    <action application="att_xfer" data="user/$2@${domain_name}"/>
  </condition>
</extension>
```

### Monitor Transfer Status

```bash
# Check transfer status via API
fs_cli -x "subscribe_status 123456789@example.com"

# Monitor in real-time
fs_cli -x "console loglevel 7"
# Watch for "Dialog SUBSCRIBE" and "Transfer" messages
```

### PJSIP Integration

When PJSIP sends a SUBSCRIBE message during a transfer:

```sip
SUBSCRIBE sip:user@freeswitch.example.com SIP/2.0
Event: dialog
Expires: 3600
Accept: application/dialog-info+xml
```

FreeSWITCH responds with:

```sip
SIP/2.0 200 OK
Content-Type: application/dialog-info+xml
Expires: 3600

<?xml version="1.0"?>
<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" 
             version="1" state="full" entity="sip:user@freeswitch.example.com">
  <dialog id="123456789@example.com" call-id="123456789@example.com" direction="initiator">
    <state>trying</state>
    <local>
      <identity>sip:user@freeswitch.example.com</identity>
    </local>
    <remote>
      <identity>sip:target@freeswitch.example.com</identity>
    </remote>
  </dialog>
</dialog-info>
```

## Testing

### Unit Testing

```bash
# Build and test
make test

# Load module and test API
fs_cli -x "load mod_subscribe_handler"
fs_cli -x "subscribe_status test_call_id"
```

### Integration Testing

1. **Set up test scenario:**
   ```bash
   # Start FreeSWITCH with the module loaded
   # Configure PJSIP to send SUBSCRIBE messages
   # Initiate a transfer that takes longer than 60 seconds
   ```

2. **Monitor dialog states:**
   ```bash
   # Watch FreeSWITCH logs
   tail -f /usr/local/freeswitch/log/freeswitch.log | grep -i "dialog\|subscribe\|transfer"
   ```

3. **Verify PJSIP response:**
   ```bash
   # Check that PJSIP receives 200 OK with dialog-info XML
   # instead of 40x responses
   ```

## Troubleshooting

### Common Issues

1. **Module fails to load:**
   ```bash
   # Check FreeSWITCH logs
   fs_cli -x "console loglevel 7"
   # Verify include paths in Makefile
   ```

2. **SUBSCRIBE still returns 40x:**
   ```bash
   # Ensure Sofia patch is properly applied
   # Check that transfer state variables are set
   # Verify dialog event subscription type
   ```

3. **Transfer state not updating:**
   ```bash
   # Check dialplan configuration
   # Verify event bindings in module
   # Monitor channel events
   ```

### Debug Mode

```bash
# Build with debug symbols
make debug

# Enable verbose logging
fs_cli -x "console loglevel 7"
fs_cli -x "sofia loglevel all 9"
```

## Technical Details

### Dialog State Machine

```
Transfer Initiated → trying
     ↓
Target Ringing → early/trying (continues for up to 200 seconds)
     ↓
Target Answers → confirmed
     ↓
Transfer Complete → terminated
```

### Event Flow

1. **CHANNEL_BRIDGE**: Transfer initiated
2. **CHANNEL_PROGRESS**: Target ringing
3. **CHANNEL_ANSWER**: Target answered
4. **CHANNEL_UNBRIDGE**: Transfer completed

### SIP Dialog States

- **trying**: Initial state, transfer in progress
- **early**: Target is alerting/ringing
- **confirmed**: Active call established
- **terminated**: Call/transfer ended

## Compliance

This solution follows:
- **RFC 3261**: SIP Protocol specification
- **RFC 4235**: Dialog Event Package for SIP
- **RFC 3265**: SIP Event Notification Framework

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is provided as-is for educational and development purposes. Please ensure compliance with your FreeSWITCH license and local regulations.

## Support

For issues and questions:
1. Check the troubleshooting section
2. Review FreeSWITCH logs
3. Test with debug mode enabled
4. Consult FreeSWITCH documentation

## Version History

- **1.0.0**: Initial release
  - Basic SUBSCRIBE handling for transfers
  - Dialog state notifications
  - Transfer state tracking
  - Sofia SIP integration

---

**Note**: This solution requires careful integration with your existing FreeSWITCH configuration. Test thoroughly in a development environment before deploying to production.
