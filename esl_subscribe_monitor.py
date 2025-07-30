#!/usr/bin/env python3

"""
FreeSWITCH SUBSCRIBE Handler via ESL (Event Socket Library)
This script monitors FreeSWITCH events and handles SUBSCRIBE messages
for transfer scenarios without requiring source code changes.
"""

import ESL
import time
import sqlite3
import xml.etree.ElementTree as ET
import logging
import threading
import re
from datetime import datetime, timedelta

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class SubscribeHandler:
    def __init__(self, host='127.0.0.1', port=8021, password='ClueCon'):
        self.host = host
        self.port = port
        self.password = password
        self.connection = None
        self.db_path = '/tmp/freeswitch_transfer_states.db'
        self.transfer_timeout = 200  # seconds
        self.subscribe_expires = 3600  # seconds
        self.running = False
        
        # Initialize database
        self.init_database()
        
    def init_database(self):
        """Initialize SQLite database for transfer state tracking"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            cursor.execute('''
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
            ''')
            
            conn.commit()
            conn.close()
            logger.info("Database initialized successfully")
            
        except Exception as e:
            logger.error(f"Failed to initialize database: {e}")
    
    def store_transfer_state(self, call_id, state, target_uri=None, from_user=None, 
                           to_user=None, from_host=None, to_host=None):
        """Store transfer state in database"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            current_time = int(time.time())
            
            cursor.execute('''
                INSERT OR REPLACE INTO transfer_states 
                (call_id, state, start_time, last_update, target_uri, 
                 from_user, to_user, from_host, to_host)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            ''', (call_id, state, current_time, current_time, 
                  target_uri or '', from_user or '', to_user or '', 
                  from_host or '', to_host or ''))
            
            conn.commit()
            conn.close()
            logger.debug(f"Stored transfer state: {call_id} -> {state}")
            
        except Exception as e:
            logger.error(f"Failed to store transfer state: {e}")
    
    def get_transfer_state(self, call_id):
        """Get transfer state from database"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            cursor.execute('''
                SELECT * FROM transfer_states WHERE call_id = ?
            ''', (call_id,))
            
            result = cursor.fetchone()
            conn.close()
            
            if result:
                return {
                    'call_id': result[0],
                    'state': result[1],
                    'start_time': result[2],
                    'last_update': result[3],
                    'target_uri': result[4],
                    'from_user': result[5],
                    'to_user': result[6],
                    'from_host': result[7],
                    'to_host': result[8]
                }
            return None
            
        except Exception as e:
            logger.error(f"Failed to get transfer state: {e}")
            return None
    
    def update_transfer_state(self, call_id, new_state):
        """Update transfer state in database"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            current_time = int(time.time())
            
            cursor.execute('''
                UPDATE transfer_states 
                SET state = ?, last_update = ? 
                WHERE call_id = ?
            ''', (new_state, current_time, call_id))
            
            conn.commit()
            conn.close()
            logger.debug(f"Updated transfer state: {call_id} -> {new_state}")
            
        except Exception as e:
            logger.error(f"Failed to update transfer state: {e}")
    
    def get_dialog_state_for_transfer(self, transfer_record):
        """Determine dialog state based on transfer state and timing"""
        if not transfer_record:
            return "terminated"
        
        current_time = int(time.time())
        elapsed = current_time - transfer_record['start_time']
        
        logger.debug(f"Transfer state: {transfer_record['state']}, elapsed: {elapsed} seconds")
        
        if elapsed > self.transfer_timeout:
            return "terminated"
        
        if transfer_record['state'] in ['trying', 'ringing']:
            return "trying"  # Still in progress
        elif transfer_record['state'] in ['answered', 'confirmed']:
            return "confirmed"
        elif transfer_record['state'] in ['completed', 'failed']:
            return "terminated"
        else:
            return "trying"  # Default for unknown states
    
    def generate_dialog_info_xml(self, call_id, dialog_state, from_user, from_host, 
                                to_user, to_host, entity):
        """Generate dialog-info XML for NOTIFY response"""
        version = str(int(time.time()) % 999999)
        
        xml_template = f'''<?xml version="1.0"?>
<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" 
             version="{version}" state="full" entity="sip:{entity}">
  <dialog id="{call_id}" call-id="{call_id}" direction="initiator">
    <state>{dialog_state}</state>
    <local>
      <identity>sip:{from_user}@{from_host}</identity>
      <target uri="sip:{from_user}@{from_host}"/>
    </local>
    <remote>
      <identity>sip:{to_user}@{to_host}</identity>
      <target uri="sip:{to_user}@{to_host}"/>
    </remote>
  </dialog>
</dialog-info>'''
        
        return xml_template.strip()
    
    def handle_subscribe_event(self, event_headers):
        """Handle SUBSCRIBE message from event"""
        call_id = event_headers.get('sip_call_id') or event_headers.get('variable_sip_call_id')
        event_type = event_headers.get('sip_h_Event') or event_headers.get('Event')
        from_user = event_headers.get('sip_from_user') or event_headers.get('variable_sip_from_user')
        from_host = event_headers.get('sip_from_host') or event_headers.get('variable_sip_from_host')
        to_user = event_headers.get('sip_to_user') or event_headers.get('variable_sip_to_user')
        to_host = event_headers.get('sip_to_host') or event_headers.get('variable_sip_to_host')
        contact = event_headers.get('sip_contact_uri')
        expires = event_headers.get('sip_h_Expires', str(self.subscribe_expires))
        
        logger.info(f"SUBSCRIBE received: Call-ID={call_id}, Event={event_type}")
        
        # Only handle dialog events
        if not event_type or 'dialog' not in event_type.lower():
            logger.debug(f"Ignoring non-dialog SUBSCRIBE: {event_type}")
            return False
        
        if not call_id:
            logger.warning("SUBSCRIBE without Call-ID, ignoring")
            return False
        
        # Get transfer state
        transfer_record = self.get_transfer_state(call_id)
        dialog_state = self.get_dialog_state_for_transfer(transfer_record)
        
        # Generate entity URI
        entity = f"{from_user or 'unknown'}@{from_host or 'localhost'}"
        
        # Generate dialog-info XML
        dialog_xml = self.generate_dialog_info_xml(
            call_id, dialog_state,
            from_user or 'unknown', from_host or 'localhost',
            to_user or 'unknown', to_host or 'localhost',
            entity
        )
        
        # Send NOTIFY response
        self.send_notify_response(contact or entity, dialog_xml, expires)
        
        logger.info(f"Sent NOTIFY with state '{dialog_state}' for Call-ID: {call_id}")
        return True
    
    def send_notify_response(self, target, dialog_xml, expires):
        """Send NOTIFY response using FreeSWITCH API"""
        try:
            if not self.connection:
                return False
            
            # Escape XML for command line
            escaped_xml = dialog_xml.replace('"', '\\"').replace("'", "\\'")
            
            # Send NOTIFY via sofia
            notify_cmd = f"sofia global notify {target} Event dialog Content-Type application/dialog-info+xml '{escaped_xml}'"
            
            response = self.connection.api(notify_cmd)
            
            if response:
                logger.debug(f"NOTIFY command response: {response.getBody()}")
            
            return True
            
        except Exception as e:
            logger.error(f"Failed to send NOTIFY: {e}")
            return False
    
    def handle_channel_event(self, event):
        """Handle channel events for transfer state tracking"""
        try:
            event_name = event.getHeader('Event-Name')
            call_id = (event.getHeader('variable_sip_call_id') or 
                      event.getHeader('Caller-Unique-ID') or 
                      event.getHeader('Unique-ID'))
            
            if not call_id:
                return
            
            logger.debug(f"Channel event: {event_name} for Call-ID: {call_id}")
            
            if event_name == 'CHANNEL_BRIDGE':
                # Transfer/bridge initiated
                bridge_to = (event.getHeader('Bridge-To-UUID') or 
                           event.getHeader('Other-Leg-Unique-ID'))
                
                if bridge_to:
                    from_user = event.getHeader('variable_sip_from_user')
                    to_user = event.getHeader('variable_sip_to_user')
                    from_host = event.getHeader('variable_sip_from_host')
                    to_host = event.getHeader('variable_sip_to_host')
                    
                    self.store_transfer_state(call_id, 'trying', bridge_to, 
                                            from_user, to_user, from_host, to_host)
                    logger.info(f"Transfer initiated: {call_id} -> {bridge_to}")
            
            elif event_name in ['CHANNEL_PROGRESS', 'CHANNEL_PROGRESS_MEDIA']:
                # Target is ringing
                transfer_record = self.get_transfer_state(call_id)
                if transfer_record:
                    self.update_transfer_state(call_id, 'ringing')
            
            elif event_name == 'CHANNEL_ANSWER':
                # Target answered
                transfer_record = self.get_transfer_state(call_id)
                if transfer_record:
                    self.update_transfer_state(call_id, 'answered')
            
            elif event_name in ['CHANNEL_UNBRIDGE', 'CHANNEL_HANGUP']:
                # Transfer completed or failed
                transfer_record = self.get_transfer_state(call_id)
                if transfer_record:
                    self.update_transfer_state(call_id, 'completed')
        
        except Exception as e:
            logger.error(f"Error handling channel event: {e}")
    
    def handle_sip_event(self, event):
        """Handle SIP-related events that might contain SUBSCRIBE"""
        try:
            event_name = event.getHeader('Event-Name')
            sip_method = event.getHeader('sip_method')
            
            if sip_method and sip_method.upper() == 'SUBSCRIBE':
                # Extract headers from event
                event_headers = {}
                
                # Get all headers
                header_names = ['sip_call_id', 'sip_h_Event', 'sip_from_user', 
                               'sip_from_host', 'sip_to_user', 'sip_to_host', 
                               'sip_contact_uri', 'sip_h_Expires']
                
                for header in header_names:
                    value = event.getHeader(header)
                    if value:
                        event_headers[header] = value
                
                self.handle_subscribe_event(event_headers)
        
        except Exception as e:
            logger.error(f"Error handling SIP event: {e}")
    
    def connect(self):
        """Connect to FreeSWITCH via ESL"""
        try:
            self.connection = ESL.ESLconnection(self.host, str(self.port), self.password)
            
            if not self.connection.connected():
                logger.error("Failed to connect to FreeSWITCH")
                return False
            
            logger.info(f"Connected to FreeSWITCH at {self.host}:{self.port}")
            
            # Subscribe to events
            events = [
                'CHANNEL_BRIDGE', 'CHANNEL_PROGRESS', 'CHANNEL_PROGRESS_MEDIA',
                'CHANNEL_ANSWER', 'CHANNEL_UNBRIDGE', 'CHANNEL_HANGUP',
                'RECV_INFO', 'CUSTOM', 'MESSAGE'
            ]
            
            for event in events:
                response = self.connection.events('plain', event)
                if response:
                    logger.debug(f"Subscribed to {event}: {response.getBody()}")
            
            return True
            
        except Exception as e:
            logger.error(f"Connection error: {e}")
            return False
    
    def run(self):
        """Main event loop"""
        if not self.connect():
            return
        
        self.running = True
        logger.info("Subscribe handler started, listening for events...")
        
        try:
            while self.running:
                event = self.connection.recvEvent()
                
                if event:
                    event_name = event.getHeader('Event-Name')
                    
                    # Handle channel events for transfer tracking
                    if event_name.startswith('CHANNEL_'):
                        self.handle_channel_event(event)
                    
                    # Handle SIP events that might contain SUBSCRIBE
                    elif event_name in ['RECV_INFO', 'CUSTOM', 'MESSAGE']:
                        self.handle_sip_event(event)
                
                # Small delay to prevent busy waiting
                time.sleep(0.01)
        
        except KeyboardInterrupt:
            logger.info("Received interrupt signal")
        except Exception as e:
            logger.error(f"Event loop error: {e}")
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Cleanup resources"""
        self.running = False
        if self.connection:
            self.connection.disconnect()
        logger.info("Subscribe handler stopped")
    
    def get_status(self, call_id):
        """Get transfer status for a specific call ID"""
        transfer_record = self.get_transfer_state(call_id)
        
        if transfer_record:
            current_time = int(time.time())
            elapsed = current_time - transfer_record['start_time']
            dialog_state = self.get_dialog_state_for_transfer(transfer_record)
            
            return {
                'call_id': call_id,
                'state': transfer_record['state'],
                'dialog_state': dialog_state,
                'elapsed': elapsed,
                'target': transfer_record['target_uri'],
                'last_update': datetime.fromtimestamp(transfer_record['last_update']).isoformat()
            }
        else:
            return {'call_id': call_id, 'status': 'No transfer state found'}

def main():
    """Main function"""
    import sys
    
    if len(sys.argv) > 1:
        command = sys.argv[1]
        
        if command == 'status' and len(sys.argv) > 2:
            # Status command
            call_id = sys.argv[2]
            handler = SubscribeHandler()
            status = handler.get_status(call_id)
            
            for key, value in status.items():
                print(f"{key}: {value}")
            return
        
        elif command == 'help':
            print("Usage:")
            print("  python3 esl_subscribe_monitor.py          - Start event listener")
            print("  python3 esl_subscribe_monitor.py status <call-id>  - Get status")
            print("  python3 esl_subscribe_monitor.py help     - Show this help")
            return
    
    # Default: run event listener
    handler = SubscribeHandler()
    
    try:
        handler.run()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
        handler.cleanup()

if __name__ == '__main__':
    main()