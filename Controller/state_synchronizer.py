from oscpy.client import OSCClient
from oscpy.server import OSCThreadServer
import threading
import asyncio
from bs4 import BeautifulSoup
import time
import sys
import ssl
import traceback

# If USE_WEBSOCKETS is set to True, WebSockets will be used to communicate with app, otherwise OSC 
# will be used
USE_WEBSOCKETS = False
if USE_WEBSOCKETS:
    import websocket


from flask import Flask
state_debugger_port = 5100
flask_app = Flask(__name__)

@flask_app.route('/')
def state_debugger():
    if sss_instance is None:
        return 'No ShepherdStateSynchronizer yet'
    else:
        return str(sss_instance.state_soup)


osc_send_host = "127.0.0.1"
osc_send_port = 9003
osc_receive_port = 9004



class StateDebuggerServerThread(threading.Thread):

    def __init__(self, port, source_state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.source_state_synchronizer = source_state_synchronizer

    def run(self):
        #asyncio.set_event_loop(asyncio.new_event_loop())
        print('* Starting state debugger in port {}'.format(self.port))
        flask_app.run(host='0.0.0.0', port=self.port, debug=True, use_reloader=False)


sss_instance = None
volatile_state_refresh_fps = 0  # Shepherd uses no volatile state


def state_update_handler(*values):
    update_type = values[0]
    update_id = values[1]
    update_data = values[2:]
    if sss_instance is not None:
        sss_instance.apply_update(update_id, update_type, update_data)
    

def full_state_handler(*values):
    update_id = values[0]
    new_state_raw = values[1]
    if sss_instance is not None:
        sss_instance.set_full_state(update_id, new_state_raw)


def volatile_state_string_handler(*values):
    volatile_state_string = values[0]
    if sss_instance is not None:
        sss_instance.set_volatile_state_from_string(volatile_state_string)


def osc_state_update_handler(*values):
    new_values = []
    for value in values:
        if type(value) == bytes:
            new_values.append(str(value.decode('utf-8')))
        else:
            new_values.append(value)
    state_update_handler(*new_values)


def osc_full_state_handler(*values):
    new_values = []
    for value in values:
        if type(value) == bytes:
            new_values.append(str(value.decode('utf-8')))
        else:
            new_values.append(value)
    full_state_handler(*new_values)


def osc_volatile_state_string_handler(*values):
    new_values = []
    for value in values:
        if type(value) == bytes:
            new_values.append(str(value.decode('utf-8')))
        else:
            new_values.append(value)
    volatile_state_string_handler(*new_values)

    


class OSCReceiverThread(threading.Thread):

    def __init__(self, port, source_state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.source_state_synchronizer = source_state_synchronizer

    def run(self):
        #asyncio.set_event_loop(asyncio.new_event_loop())
        osc = OSCThreadServer()
        print('* Listening OSC messages in port {}'.format(self.port))
        osc.listen(address='0.0.0.0', port=self.port, default=True)
        osc.bind(b'/app_started', lambda: sss_instance.app_has_started())
        osc.bind(b'/alive', lambda: sss_instance.app_is_alive())
        osc.bind(b'/state_update', osc_state_update_handler)
        osc.bind(b'/full_state', osc_full_state_handler)
        osc.bind(b'/volatile_state_string', osc_volatile_state_string_handler)

        # Legacy methods
        osc.bind(b'/shepherdReady', lambda *values: sss_instance.app.shepherd_interface.receive_shepherd_ready())
        osc.bind(b'/stateFromShepherd', lambda *values: sss_instance.app.shepherd_interface.receive_state_from_shepherd(*values))
        osc.bind(b'/midiCCParameterValuesForDevice', lambda *values: sss_instance.app.shepherd_interface.receive_midi_cc_values_for_device(*values))
        

def ws_on_message(ws, message):
    if sss_instance is not None:
        sss_instance.ws_connection_ok = True

    address = message[:message.find(':')]
    data = message[message.find(':') + 1:]
    
    if address == '/volatile_state_string':
        volatile_state_string_handler(data)

    elif address == '/app_started':
        sss_instance.app_has_started()

    elif address == '/state_update':
        data_parts = data.split(';')
        update_type = data_parts[0]
        update_id = int(data_parts[1])
        if update_type == "propertyChanged" or update_type == "removedChild":
            # Can safely use the data parts after splitting by ; because we know no ; characters would be in the data
            update_data = data_parts[2:]
        elif update_type == "addedChild":
            # Can't directly use split by ; because the XML state portion might contain ; characters inside, need to be careful
            update_data = [data_parts[2], data_parts[3], data_parts[4], ';'.join(data_parts[5:])]
        args = [update_type, update_id] + update_data
        state_update_handler(*args)

    elif address == '/full_state':
        # Split data at first ocurrence of ; instead of all ocurrences of ; as character ; might be in XML state portion
        split_at = data.find(';')
        data_parts = data[:split_at], data[split_at + 1:]
        update_id = int(data_parts[0])
        full_state_raw = data_parts[1]
        args = [update_id, full_state_raw]
        full_state_handler(*args)
    

def ws_on_error(ws, error):
    print("* WS connection error: {}".format(error))
    if 'Connection refused' not in str(error) and 'WebSocketConnectionClosedException' not in str(error):
        print(traceback.format_exc())
        
    if sss_instance is not None:
        sss_instance.ws_connection_ok = False


def ws_on_close(ws, close_status_code, close_msg):
    print("* WS connection closed: {} - {}".format(close_status_code, close_msg))
    if sss_instance is not None:
        sss_instance.ws_connection_ok = False


def ws_on_open(ws):
    print("* WS connection opened")
    if sss_instance is not None:
        sss_instance.ws_connection_ok = True


class WSConnectionThread(threading.Thread):

    reconnect_interval = 2
    last_time_tried_wss = True  # Start trying ws connection (instead of wss)

    def __init__(self, port, source_state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.source_state_synchronizer = source_state_synchronizer

    def run(self):
        # Try to establish a connection with the websockets server
        # If it can't be established, tries again every self.reconnect_interval seconds
        # Because we don't know if the server will use wss or not, we alternatively try
        # one or the other
        #asyncio.set_event_loop(asyncio.new_event_loop())
        while True:
            try_wss = not self.last_time_tried_wss
            self.last_time_tried_wss = not self.last_time_tried_wss
            ws_endpoint = "ws{}://localhost:{}/source_coms/".format('s' if try_wss else '', self.port)
            ws = websocket.WebSocketApp(ws_endpoint,
                                    on_open=ws_on_open,
                                    on_message=ws_on_message,
                                    on_error=ws_on_error,
                                    on_close=ws_on_close)
            self.source_state_synchronizer.ws_connection = ws
            print('* Connecting to WS server: {}'.format(ws_endpoint))
            ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE}, skip_utf8_validation=True)
            print('WS connection lost - will try connecting again in {} seconds'.format(self.reconnect_interval))
            time.sleep(self.reconnect_interval)
    

class RequestStateThread(threading.Thread):

    def __init__(self, source_state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.source_state_synchronizer = source_state_synchronizer

    def run(self):
        #asyncio.set_event_loop(asyncio.new_event_loop())
        print('* Starting loop to request state')
        while True:
            time.sleep(1.0/volatile_state_refresh_fps)
            self.source_state_synchronizer.request_volatile_state()
            if self.source_state_synchronizer.should_request_full_state:
                self.source_state_synchronizer.request_full_state()


class ShepherdStateSynchronizer(object):

    osc_client = None
    last_time_app_alive = 0  # Only used when using OSC communication

    ws_connection = None
    ws_connection_ok = False

    last_update_id = -1
    should_request_full_state = False
    full_state_requested = False

    state_soup = None
    volatile_state = {}
    app = None

    verbose = False

    def __init__(self, app, osc_ip=osc_send_host, osc_port_send=osc_send_port, osc_port_receive=osc_receive_port, ws_port=8125, verbose=True):
        self.app = app
        
        global sss_instance
        sss_instance = self
        self.verbose = verbose
        
        if not USE_WEBSOCKETS:
            print('* Using OSC to communicate with app')

            # Start OSC receiver to receive OSC messages from the app
            OSCReceiverThread(osc_port_receive, self).start()
            
            # Start OSC client to send OSC messages to app
            self.osc_client = OSCClient(osc_ip, osc_port_send, encoding='utf8')
            print('* Sending OSC messages in port {}'.format(osc_port_send))
        else:
            print('* Using WebSockets to communicate with app')

            # Start websockets client to handle communication with app
            WSConnectionThread(ws_port, self).start()

        # Start Thread to request state to app
        # This thread will request mostly the volatile state, but will also occasioanally
        # request full state if self.should_request_full_state is True
        if volatile_state_refresh_fps > 0:
            RequestStateThread(self).start()

        # If state debugger is enabled, start  the server
        if flask_app is not None:
            StateDebuggerServerThread(state_debugger_port, self).start()

        time.sleep(0.5)  # Give time for threads to start
        self.should_request_full_state = True
        self.request_full_state()

    def send_msg_to_app(self, address, values):
        if self.osc_client is not None:
            self.osc_client.send_message(address, values)
        if self.ws_connection is not None and self.ws_connection_ok:
            self.ws_connection.send(address + ':' + ';'.join([str(value) for value in values]))

    def app_has_started(self):
        self.last_update_id = -1
        self.state_soup = None
        self.should_request_full_state = True

    def app_is_alive(self):
        self.last_time_app_alive = time.time()

    def app_may_be_down(self):
        if not USE_WEBSOCKETS:
            return time.time() - self.last_time_app_alive > 5.0  # Consider app maybe down if no alive message in 5 seconds
        else:
            return not self.ws_connection_ok  # Consider app down if no active WS connection

    def request_full_state(self):
        if not self.full_state_requested and not self.app_may_be_down():
            print('* Requesting full state')
            self.full_state_requested = True
            self.send_msg_to_app('/get_state', ["full"])

    def request_volatile_state(self):
        self.send_msg_to_app('/get_state', ["volatileString"])

    def set_full_state(self, update_id, full_state_raw):
        if self.verbose:
            print("Receiving full state with update id {}".format(update_id))
        self.state_soup = BeautifulSoup(full_state_raw, "lxml").findAll("source_state")[0]
        self.full_state_requested = False
        self.should_request_full_state = False
        #self.app.current_state.on_source_state_update()

        # Configure some stuff that requires the data paths to be known
        #configure_recent_queries_sound_usage_log_tmp_base_path_from_source_state(self.state_soup['sourceDataLocation'.lower()], self.state_soup['tmpFilesLocation'.lower()])

    def set_volatile_state_from_string(self, volatile_state_string):
        # Shepherd uses no volatile state
        pass

    def apply_update(self, update_id, update_type, update_data):
        if self.state_soup is not None:
            if self.verbose:
                print("Applying state update {} - {}".format(update_id, update_type))

            if update_type == "propertyChanged":
                tree_uuid = update_data[0]
                tree_type = update_data[1].lower()
                property_name = update_data[2].lower()
                new_value = update_data[3]
                results = self.state_soup.findAll(tree_type, {"uuid" : tree_uuid})
                if len(results) == 0:
                    # Found 0 results, initial state is not ready yet so we ignore
                    pass
                elif len(results) == 1:
                    results[0][property_name] = new_value
                else:
                    # Should never return more than one, request a full state as there will be sync issues
                    if self.verbose:
                        print('Unexpected number of results ({})'.format(len(results)))
                    self.should_request_full_state = True
            
            elif update_type == "addedChild":
                parent_tree_uuid = update_data[0]
                parent_tree_type = update_data[1].lower()
                index_in_parent_childs = int(update_data[2])
                # Only compute child_soup later, if parent is found
                results = self.state_soup.findAll(parent_tree_type, {"uuid" : parent_tree_uuid})
                if len(results) == 0:
                    # Found 0 results, initial state is not ready yet so we ignore
                    pass
                elif len(results) == 1:
                    child_soup =  next(BeautifulSoup(update_data[3], "lxml").find("body").children)
                    if index_in_parent_childs == -1:
                        results[0].append(child_soup)
                    else:
                        results[0].insert(index_in_parent_childs, child_soup)

                    # If the new child added is of type sound, add all corresponding sound samples to the sound usage log
                    if child_soup.name == "sound":
                        for sound_sample_element in child_soup.findAll("sound_sample"):
                            log_sound_used(sound_sample_element)
                    
                else:
                    # Should never return more than one, request a full state as there will be sync issues
                    if self.verbose:
                        print('Unexpected number of results ({})'.format(len(results)))
                    self.should_request_full_state = True
            
            elif update_type == "removedChild":
                child_to_remove_tree_uuid = update_data[0]
                child_to_remove_tree_type = update_data[1].lower()
                results = self.state_soup.findAll(child_to_remove_tree_type, {"uuid" : child_to_remove_tree_uuid})
                if len(results) == 0:
                    # Found 0 results, initial state is not ready yet so we ignore
                    pass
                elif len(results) == 1:
                    results[0].decompose()
                else:
                    # Should never return more than one, request a full state as there will be sync issues
                    if self.verbose:
                        print('Unexpected number of results ({})'.format(len(results)))
                    self.should_request_full_state = True
            
            # Check if update ID is correct and trigger request of full state if there are possible sync errors
            if self.last_update_id != -1 and self.last_update_id + 1 != update_id:
                self.should_request_full_state = True
            self.last_update_id = update_id

            #self.app.current_state.on_source_state_update()

