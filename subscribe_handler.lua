#!/usr/bin/env lua

-- FreeSWITCH SUBSCRIBE Handler for Transfer Scenarios
-- This Lua script handles SUBSCRIBE messages and provides dialog state notifications
-- without requiring any FreeSWITCH source code modifications

require "luasql.sqlite3"

-- Global configuration
local config = {
    log_level = "INFO",
    transfer_timeout = 200,  -- seconds
    subscribe_expires = 3600, -- seconds
    db_path = "/tmp/freeswitch_transfer_states.db",
    presence_hosts = {"127.0.0.1", "localhost"}
}

-- Initialize database for transfer state tracking
local function init_database()
    local env = luasql.sqlite3()
    local conn = env:connect(config.db_path)
    
    if conn then
        conn:execute([[
            CREATE TABLE IF NOT EXISTS transfer_states (
                call_id TEXT PRIMARY KEY,
                state TEXT NOT NULL,
                start_time INTEGER NOT NULL,
                last_update INTEGER NOT NULL,
                target_uri TEXT,
                from_user TEXT,
                to_user TEXT,
                from_host TEXT,
                to_host TEXT
            )
        ]])
        conn:close()
    end
    env:close()
end

-- Log function
local function log(level, message)
    if level == config.log_level or config.log_level == "DEBUG" then
        freeswitch.consoleLog(level, "[SUBSCRIBE_HANDLER] " .. message .. "\n")
    end
end

-- Get current time in seconds
local function get_current_time()
    return os.time()
end

-- Store transfer state in database
local function store_transfer_state(call_id, state, target_uri, from_user, to_user, from_host, to_host)
    local env = luasql.sqlite3()
    local conn = env:connect(config.db_path)
    
    if conn then
        local current_time = get_current_time()
        local sql = string.format([[
            INSERT OR REPLACE INTO transfer_states 
            (call_id, state, start_time, last_update, target_uri, from_user, to_user, from_host, to_host)
            VALUES ('%s', '%s', %d, %d, '%s', '%s', '%s', '%s', '%s')
        ]], call_id, state, current_time, current_time, 
            target_uri or "", from_user or "", to_user or "", from_host or "", to_host or "")
        
        conn:execute(sql)
        conn:close()
        log("DEBUG", "Stored transfer state: " .. call_id .. " -> " .. state)
    end
    env:close()
end

-- Get transfer state from database
local function get_transfer_state(call_id)
    local env = luasql.sqlite3()
    local conn = env:connect(config.db_path)
    local result = nil
    
    if conn then
        local cursor = conn:execute("SELECT * FROM transfer_states WHERE call_id = '" .. call_id .. "'")
        if cursor then
            result = cursor:fetch({}, "a")
            cursor:close()
        end
        conn:close()
    end
    env:close()
    
    return result
end

-- Update transfer state
local function update_transfer_state(call_id, new_state)
    local env = luasql.sqlite3()
    local conn = env:connect(config.db_path)
    
    if conn then
        local current_time = get_current_time()
        local sql = string.format(
            "UPDATE transfer_states SET state = '%s', last_update = %d WHERE call_id = '%s'",
            new_state, current_time, call_id
        )
        conn:execute(sql)
        conn:close()
        log("DEBUG", "Updated transfer state: " .. call_id .. " -> " .. new_state)
    end
    env:close()
end

-- Generate dialog-info XML
local function generate_dialog_info_xml(call_id, dialog_state, from_user, from_host, to_user, to_host, entity)
    local version = tostring(math.random(1, 999999))
    
    local xml = string.format([[<?xml version="1.0"?>
<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" 
             version="%s" state="full" entity="sip:%s">
  <dialog id="%s" call-id="%s" direction="initiator">
    <state>%s</state>
    <local>
      <identity>sip:%s@%s</identity>
      <target uri="sip:%s@%s"/>
    </local>
    <remote>
      <identity>sip:%s@%s</identity>
      <target uri="sip:%s@%s"/>
    </remote>
  </dialog>
</dialog-info>]], 
        version, entity,
        call_id, call_id, dialog_state,
        from_user, from_host, from_user, from_host,
        to_user, to_host, to_user, to_host
    )
    
    return xml
end

-- Determine dialog state based on transfer state and timing
local function get_dialog_state_for_transfer(transfer_record)
    if not transfer_record then
        return "terminated"
    end
    
    local current_time = get_current_time()
    local elapsed = current_time - transfer_record.start_time
    
    log("DEBUG", string.format("Transfer state: %s, elapsed: %d seconds", transfer_record.state, elapsed))
    
    if elapsed > config.transfer_timeout then
        return "terminated"
    end
    
    if transfer_record.state == "trying" or transfer_record.state == "ringing" then
        return "trying"  -- Still in progress
    elseif transfer_record.state == "answered" or transfer_record.state == "confirmed" then
        return "confirmed"
    elseif transfer_record.state == "completed" or transfer_record.state == "failed" then
        return "terminated"
    else
        return "trying"  -- Default for unknown states
    end
end

-- Handle SUBSCRIBE message via Event Socket
local function handle_subscribe_event(event_data)
    local call_id = event_data["sip_call_id"]
    local event_type = event_data["sip_h_Event"]
    local from_user = event_data["sip_from_user"]
    local from_host = event_data["sip_from_host"]
    local to_user = event_data["sip_to_user"] 
    local to_host = event_data["sip_to_host"]
    local contact = event_data["sip_contact_uri"]
    local expires = event_data["sip_h_Expires"] or "3600"
    
    log("INFO", string.format("SUBSCRIBE received: Call-ID=%s, Event=%s", 
        call_id or "unknown", event_type or "unknown"))
    
    -- Only handle dialog events
    if not event_type or not string.match(event_type:lower(), "dialog") then
        return false
    end
    
    if not call_id then
        log("WARN", "SUBSCRIBE without Call-ID, ignoring")
        return false
    end
    
    -- Get transfer state
    local transfer_record = get_transfer_state(call_id)
    local dialog_state = get_dialog_state_for_transfer(transfer_record)
    
    -- Generate entity URI
    local entity = string.format("%s@%s", from_user or "unknown", from_host or "localhost")
    
    -- Generate dialog-info XML
    local dialog_xml = generate_dialog_info_xml(
        call_id, dialog_state, 
        from_user or "unknown", from_host or "localhost",
        to_user or "unknown", to_host or "localhost",
        entity
    )
    
    -- Send NOTIFY response via API
    local notify_command = string.format(
        "sofia global notify %s Event dialog Content-Type application/dialog-info+xml",
        contact or (entity)
    )
    
    log("INFO", string.format("Sending NOTIFY with state '%s' for Call-ID: %s", dialog_state, call_id))
    
    -- Execute the NOTIFY
    local api = freeswitch.API()
    api:execute("sofia", string.format("notify %s dialog '%s'", contact or entity, dialog_xml))
    
    return true
end

-- Event handler for transfer state tracking
local function handle_channel_event(event)
    local event_name = event:getHeader("Event-Name")
    local call_id = event:getHeader("variable_sip_call_id") or event:getHeader("Caller-Unique-ID")
    local from_user = event:getHeader("variable_sip_from_user")
    local to_user = event:getHeader("variable_sip_to_user") 
    local from_host = event:getHeader("variable_sip_from_host")
    local to_host = event:getHeader("variable_sip_to_host")
    
    if not call_id then
        return
    end
    
    log("DEBUG", string.format("Channel event: %s for Call-ID: %s", event_name, call_id))
    
    if event_name == "CHANNEL_BRIDGE" then
        -- Transfer/bridge initiated
        local bridge_to = event:getHeader("Bridge-To-UUID") or event:getHeader("Other-Leg-Unique-ID")
        if bridge_to then
            store_transfer_state(call_id, "trying", bridge_to, from_user, to_user, from_host, to_host)
            log("INFO", string.format("Transfer initiated: %s -> %s", call_id, bridge_to))
        end
        
    elseif event_name == "CHANNEL_PROGRESS" or event_name == "CHANNEL_PROGRESS_MEDIA" then
        -- Target is ringing
        local transfer_record = get_transfer_state(call_id)
        if transfer_record then
            update_transfer_state(call_id, "ringing")
        end
        
    elseif event_name == "CHANNEL_ANSWER" then
        -- Target answered
        local transfer_record = get_transfer_state(call_id)
        if transfer_record then
            update_transfer_state(call_id, "answered")
        end
        
    elseif event_name == "CHANNEL_UNBRIDGE" or event_name == "CHANNEL_HANGUP" then
        -- Transfer completed or failed
        local transfer_record = get_transfer_state(call_id)
        if transfer_record then
            update_transfer_state(call_id, "completed")
        end
    end
end

-- Main event loop for ESL
local function run_event_listener()
    init_database()
    
    local con = freeswitch.EventConsumer("CHANNEL_BRIDGE", "CHANNEL_PROGRESS", "CHANNEL_PROGRESS_MEDIA", 
                                        "CHANNEL_ANSWER", "CHANNEL_UNBRIDGE", "CHANNEL_HANGUP",
                                        "RECV_INFO", "CUSTOM")
    
    if not con then
        log("ERROR", "Failed to create EventConsumer")
        return
    end
    
    log("INFO", "Subscribe handler started, listening for events...")
    
    while true do
        local event = con:pop(1)  -- Wait 1 second for events
        
        if event then
            local event_name = event:getHeader("Event-Name")
            
            -- Handle channel events for transfer tracking
            if string.match(event_name, "^CHANNEL_") then
                handle_channel_event(event)
            end
            
            -- Handle SUBSCRIBE via RECV_INFO or CUSTOM events
            if event_name == "RECV_INFO" or event_name == "CUSTOM" then
                local sip_method = event:getHeader("sip_method")
                if sip_method and sip_method:upper() == "SUBSCRIBE" then
                    local event_data = {}
                    
                    -- Extract SIP headers from event
                    event_data["sip_call_id"] = event:getHeader("sip_call_id")
                    event_data["sip_h_Event"] = event:getHeader("sip_h_Event")
                    event_data["sip_from_user"] = event:getHeader("sip_from_user")
                    event_data["sip_from_host"] = event:getHeader("sip_from_host")
                    event_data["sip_to_user"] = event:getHeader("sip_to_user")
                    event_data["sip_to_host"] = event:getHeader("sip_to_host")
                    event_data["sip_contact_uri"] = event:getHeader("sip_contact_uri")
                    event_data["sip_h_Expires"] = event:getHeader("sip_h_Expires")
                    
                    handle_subscribe_event(event_data)
                end
            end
        end
    end
end

-- API handler for status checking
local function api_subscribe_status(cmd, args)
    local call_id = args
    
    if not call_id or call_id == "" then
        return "Usage: lua subscribe_handler.lua status <call-id>"
    end
    
    init_database()
    local transfer_record = get_transfer_state(call_id)
    
    if transfer_record then
        local current_time = get_current_time()
        local elapsed = current_time - transfer_record.start_time
        local dialog_state = get_dialog_state_for_transfer(transfer_record)
        
        return string.format(
            "Call-ID: %s\nState: %s\nDialog State: %s\nElapsed: %d seconds\nTarget: %s\nLast Update: %s",
            call_id, transfer_record.state, dialog_state, elapsed, 
            transfer_record.target_uri, os.date("%Y-%m-%d %H:%M:%S", transfer_record.last_update)
        )
    else
        return "No transfer state found for Call-ID: " .. call_id
    end
end

-- Session-based SUBSCRIBE handler (when called from dialplan)
local function session_subscribe_handler(session)
    if not session then
        log("ERROR", "No session provided to subscribe handler")
        return
    end
    
    local call_id = session:getVariable("sip_call_id")
    local event_type = session:getVariable("sip_h_Event")
    
    log("INFO", string.format("Session SUBSCRIBE handler: Call-ID=%s, Event=%s", 
        call_id or "unknown", event_type or "unknown"))
    
    if event_type and string.match(event_type:lower(), "dialog") and call_id then
        init_database()
        
        local transfer_record = get_transfer_state(call_id)
        local dialog_state = get_dialog_state_for_transfer(transfer_record)
        
        -- Set response variables for dialplan
        session:setVariable("subscribe_response_state", dialog_state)
        session:setVariable("subscribe_response_expires", config.subscribe_expires)
        
        local from_user = session:getVariable("sip_from_user")
        local from_host = session:getVariable("sip_from_host")
        local to_user = session:getVariable("sip_to_user")
        local to_host = session:getVariable("sip_to_host")
        local entity = string.format("%s@%s", from_user or "unknown", from_host or "localhost")
        
        local dialog_xml = generate_dialog_info_xml(
            call_id, dialog_state,
            from_user or "unknown", from_host or "localhost",
            to_user or "unknown", to_host or "localhost",
            entity
        )
        
        session:setVariable("subscribe_response_xml", dialog_xml)
        
        log("INFO", string.format("Generated dialog state '%s' for session Call-ID: %s", dialog_state, call_id))
    end
end

-- Main execution logic
local function main()
    local args = {...}
    
    if #args > 0 then
        local command = args[1]
        
        if command == "listen" then
            -- Run as event listener
            run_event_listener()
        elseif command == "status" then
            -- API command for status
            return api_subscribe_status("status", args[2])
        elseif command == "session" then
            -- Handle session-based call
            session_subscribe_handler(session)
        else
            log("INFO", "Subscribe handler loaded. Use 'listen', 'status <call-id>', or 'session'")
        end
    else
        -- Default: run as session handler if session exists, otherwise as listener
        if session then
            session_subscribe_handler(session)
        else
            log("INFO", "Subscribe handler: use 'lua subscribe_handler.lua listen' to start event listener")
        end
    end
end

-- Initialize and run
init_database()
main()