#include <switch.h>

SWITCH_MODULE_DEFINITION(mod_subscribe_handler, mod_subscribe_handler_load, mod_subscribe_handler_shutdown, NULL);

static switch_status_t handle_subscribe_event(switch_core_session_t *session);
static switch_status_t send_dialog_notify(switch_core_session_t *session, const char *dialog_state);

typedef struct {
    switch_mutex_t *mutex;
    switch_hash_t *active_transfers;
    switch_memory_pool_t *pool;
} subscribe_handler_globals_t;

static subscribe_handler_globals_t globals;

/* Transfer states */
typedef enum {
    TRANSFER_STATE_NONE = 0,
    TRANSFER_STATE_INITIATED,
    TRANSFER_STATE_RINGING,
    TRANSFER_STATE_ANSWERED,
    TRANSFER_STATE_COMPLETED,
    TRANSFER_STATE_FAILED
} transfer_state_t;

typedef struct {
    char *call_id;
    char *from_tag;
    char *to_tag;
    transfer_state_t state;
    switch_time_t last_update;
    char *target_uri;
    int ring_duration;
} transfer_context_t;

static void transfer_context_destroy(transfer_context_t *context)
{
    if (context) {
        switch_safe_free(context->call_id);
        switch_safe_free(context->from_tag);
        switch_safe_free(context->to_tag);
        switch_safe_free(context->target_uri);
        switch_safe_free(context);
    }
}

static transfer_context_t *get_transfer_context(const char *call_id)
{
    transfer_context_t *context = NULL;
    
    switch_mutex_lock(globals.mutex);
    context = (transfer_context_t *)switch_core_hash_find(globals.active_transfers, call_id);
    switch_mutex_unlock(globals.mutex);
    
    return context;
}

static void set_transfer_context(const char *call_id, transfer_context_t *context)
{
    switch_mutex_lock(globals.mutex);
    switch_core_hash_insert(globals.active_transfers, call_id, context);
    switch_mutex_unlock(globals.mutex);
}

/* Handle SUBSCRIBE messages for dialog state */
static switch_status_t handle_subscribe_dialog(switch_core_session_t *session, switch_event_t *event)
{
    const char *call_id = switch_event_get_header(event, "sip_call_id");
    const char *event_type = switch_event_get_header(event, "Event");
    const char *accept_header = switch_event_get_header(event, "Accept");
    transfer_context_t *transfer_ctx;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    
    if (!call_id || !event_type) {
        return SWITCH_STATUS_FALSE;
    }
    
    /* Check if this is a dialog event subscription */
    if (strcasecmp(event_type, "dialog") != 0) {
        return SWITCH_STATUS_FALSE;
    }
    
    /* Get transfer context */
    transfer_ctx = get_transfer_context(call_id);
    
    if (transfer_ctx) {
        switch_time_t current_time = switch_time_now();
        int elapsed_seconds = (int)((current_time - transfer_ctx->last_update) / 1000000);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                         "Dialog SUBSCRIBE for call-id %s, transfer state: %d, elapsed: %d seconds\n", 
                         call_id, transfer_ctx->state, elapsed_seconds);
        
        /* Determine response based on transfer state and timing */
        switch (transfer_ctx->state) {
            case TRANSFER_STATE_INITIATED:
            case TRANSFER_STATE_RINGING:
                if (elapsed_seconds < 200) {  /* Still within timeout window */
                    /* Send 200 OK with "still ringing" dialog state */
                    send_dialog_notify(session, "trying");
                    return SWITCH_STATUS_SUCCESS;
                } else {
                    /* Timeout - send terminating notification */
                    send_dialog_notify(session, "terminated");
                    return SWITCH_STATUS_SUCCESS;
                }
                break;
                
            case TRANSFER_STATE_ANSWERED:
                send_dialog_notify(session, "confirmed");
                return SWITCH_STATUS_SUCCESS;
                
            case TRANSFER_STATE_COMPLETED:
                send_dialog_notify(session, "terminated");
                return SWITCH_STATUS_SUCCESS;
                
            case TRANSFER_STATE_FAILED:
                send_dialog_notify(session, "terminated");
                return SWITCH_STATUS_SUCCESS;
                
            default:
                break;
        }
    } else {
        /* No active transfer - check channel state */
        switch_channel_state_t state = switch_channel_get_state(channel);
        
        switch (state) {
            case CS_RING:
            case CS_EARLY:
                send_dialog_notify(session, "early");
                return SWITCH_STATUS_SUCCESS;
                
            case CS_EXECUTE:
            case CS_CONSUME_MEDIA:
                send_dialog_notify(session, "confirmed");
                return SWITCH_STATUS_SUCCESS;
                
            case CS_HANGUP:
            case CS_DESTROY:
                send_dialog_notify(session, "terminated");
                return SWITCH_STATUS_SUCCESS;
                
            default:
                send_dialog_notify(session, "trying");
                return SWITCH_STATUS_SUCCESS;
        }
    }
    
    return SWITCH_STATUS_FALSE;
}

/* Send NOTIFY with dialog state information */
static switch_status_t send_dialog_notify(switch_core_session_t *session, const char *dialog_state)
{
    switch_event_t *notify_event;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    const char *call_id = switch_channel_get_variable(channel, "sip_call_id");
    const char *from_user = switch_channel_get_variable(channel, "sip_from_user");
    const char *to_user = switch_channel_get_variable(channel, "sip_to_user");
    char *dialog_info_xml;
    
    if (switch_event_create(&notify_event, SWITCH_EVENT_NOTIFY) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }
    
    /* Build dialog-info XML body */
    dialog_info_xml = switch_mprintf(
        "<?xml version=\"1.0\"?>\n"
        "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
        "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
        "  <dialog id=\"%s\" call-id=\"%s\" local-tag=\"%s\" remote-tag=\"%s\" direction=\"initiator\">\n"
        "    <state>%s</state>\n"
        "    <local>\n"
        "      <identity>sip:%s@%s</identity>\n"
        "      <target uri=\"sip:%s@%s\"/>\n"
        "    </local>\n"
        "    <remote>\n"
        "      <identity>sip:%s@%s</identity>\n"
        "      <target uri=\"sip:%s@%s\"/>\n"
        "    </remote>\n"
        "  </dialog>\n"
        "</dialog-info>\n",
        from_user ? from_user : "unknown",
        switch_channel_get_variable(channel, "sip_from_host"),
        call_id ? call_id : "unknown",
        call_id ? call_id : "unknown",
        switch_channel_get_variable(channel, "sip_from_tag"),
        switch_channel_get_variable(channel, "sip_to_tag"),
        dialog_state,
        from_user ? from_user : "unknown",
        switch_channel_get_variable(channel, "sip_from_host"),
        from_user ? from_user : "unknown",
        switch_channel_get_variable(channel, "sip_from_host"),
        to_user ? to_user : "unknown",
        switch_channel_get_variable(channel, "sip_to_host"),
        to_user ? to_user : "unknown",
        switch_channel_get_variable(channel, "sip_to_host")
    );
    
    /* Set NOTIFY headers */
    switch_event_add_header_string(notify_event, SWITCH_STACK_BOTTOM, "profile", "internal");
    switch_event_add_header_string(notify_event, SWITCH_STACK_BOTTOM, "event-string", "dialog");
    switch_event_add_header_string(notify_event, SWITCH_STACK_BOTTOM, "user", from_user);
    switch_event_add_header_string(notify_event, SWITCH_STACK_BOTTOM, "host", 
                                  switch_channel_get_variable(channel, "sip_from_host"));
    switch_event_add_header_string(notify_event, SWITCH_STACK_BOTTOM, "content-type", "application/dialog-info+xml");
    
    if (dialog_info_xml) {
        switch_event_add_body(notify_event, "%s", dialog_info_xml);
        switch_safe_free(dialog_info_xml);
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                     "Sending NOTIFY with dialog state: %s for call-id: %s\n", 
                     dialog_state, call_id ? call_id : "unknown");
    
    /* Fire the event */
    switch_event_fire(&notify_event);
    
    return SWITCH_STATUS_SUCCESS;
}

/* Event handler for transfer state tracking */
static void transfer_state_handler(switch_event_t *event)
{
    const char *event_name = switch_event_get_header(event, "Event-Name");
    const char *call_id = switch_event_get_header(event, "variable_sip_call_id");
    transfer_context_t *context;
    
    if (!call_id) {
        return;
    }
    
    if (!strcmp(event_name, "CHANNEL_BRIDGE")) {
        /* Transfer initiated */
        context = switch_core_alloc(globals.pool, sizeof(transfer_context_t));
        context->call_id = switch_core_strdup(globals.pool, call_id);
        context->state = TRANSFER_STATE_INITIATED;
        context->last_update = switch_time_now();
        context->ring_duration = 0;
        
        set_transfer_context(call_id, context);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                         "Transfer initiated for call-id: %s\n", call_id);
                         
    } else if (!strcmp(event_name, "CHANNEL_PROGRESS") || !strcmp(event_name, "CHANNEL_PROGRESS_MEDIA")) {
        /* Target is ringing */
        context = get_transfer_context(call_id);
        if (context) {
            context->state = TRANSFER_STATE_RINGING;
            context->last_update = switch_time_now();
        }
        
    } else if (!strcmp(event_name, "CHANNEL_ANSWER")) {
        /* Target answered */
        context = get_transfer_context(call_id);
        if (context) {
            context->state = TRANSFER_STATE_ANSWERED;
            context->last_update = switch_time_now();
        }
        
    } else if (!strcmp(event_name, "CHANNEL_UNBRIDGE") || !strcmp(event_name, "CHANNEL_HANGUP")) {
        /* Transfer completed or failed */
        context = get_transfer_context(call_id);
        if (context) {
            context->state = TRANSFER_STATE_COMPLETED;
            context->last_update = switch_time_now();
        }
    }
}

/* API command for testing */
SWITCH_STANDARD_API(subscribe_status_api)
{
    if (zstr(cmd)) {
        stream->write_function(stream, "Usage: subscribe_status <call-id>\n");
        return SWITCH_STATUS_SUCCESS;
    }
    
    transfer_context_t *context = get_transfer_context(cmd);
    if (context) {
        stream->write_function(stream, "Call-ID: %s\nState: %d\nLast Update: %ld\nTarget: %s\n",
                              context->call_id,
                              context->state,
                              context->last_update,
                              context->target_uri ? context->target_uri : "unknown");
    } else {
        stream->write_function(stream, "No transfer context found for call-id: %s\n", cmd);
    }
    
    return SWITCH_STATUS_SUCCESS;
}

/* Module load function */
SWITCH_MODULE_LOAD_FUNCTION(mod_subscribe_handler_load)
{
    switch_api_interface_t *api_interface;
    
    /* Initialize globals */
    memset(&globals, 0, sizeof(globals));
    globals.pool = pool;
    switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, globals.pool);
    switch_core_hash_init(&globals.active_transfers, globals.pool);
    
    /* Connect module API */
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    
    api_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_API_INTERFACE);
    api_interface->interface_name = "subscribe_status";
    api_interface->function = subscribe_status_api;
    api_interface->desc = "Show subscribe/transfer status for call-id";
    
    /* Bind to events */
    if (switch_event_bind(modname, SWITCH_EVENT_CHANNEL_BRIDGE, NULL, 
                         transfer_state_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not bind to CHANNEL_BRIDGE\n");
        return SWITCH_STATUS_GENERR;
    }
    
    if (switch_event_bind(modname, SWITCH_EVENT_CHANNEL_PROGRESS, NULL, 
                         transfer_state_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not bind to CHANNEL_PROGRESS\n");
        return SWITCH_STATUS_GENERR;
    }
    
    if (switch_event_bind(modname, SWITCH_EVENT_CHANNEL_ANSWER, NULL, 
                         transfer_state_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not bind to CHANNEL_ANSWER\n");
        return SWITCH_STATUS_GENERR;
    }
    
    if (switch_event_bind(modname, SWITCH_EVENT_CHANNEL_UNBRIDGE, NULL, 
                         transfer_state_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not bind to CHANNEL_UNBRIDGE\n");
        return SWITCH_STATUS_GENERR;
    }
    
    if (switch_event_bind(modname, SWITCH_EVENT_CHANNEL_HANGUP, NULL, 
                         transfer_state_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not bind to CHANNEL_HANGUP\n");
        return SWITCH_STATUS_GENERR;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Subscribe Handler module loaded successfully\n");
    
    return SWITCH_STATUS_SUCCESS;
}

/* Module shutdown function */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_subscribe_handler_shutdown)
{
    switch_event_unbind_callback(transfer_state_handler);
    
    /* Cleanup active transfers */
    switch_mutex_lock(globals.mutex);
    switch_hash_index_t *hi;
    for (hi = switch_core_hash_first(globals.active_transfers); hi; hi = switch_core_hash_next(&hi)) {
        void *val;
        switch_core_hash_this(hi, NULL, NULL, &val);
        if (val) {
            transfer_context_destroy((transfer_context_t *)val);
        }
    }
    switch_core_hash_destroy(&globals.active_transfers);
    switch_mutex_unlock(globals.mutex);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Subscribe Handler module unloaded\n");
    
    return SWITCH_STATUS_SUCCESS;
}