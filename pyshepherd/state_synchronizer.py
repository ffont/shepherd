import asyncio
import ssl
import threading
import time
import traceback
import websocket

from bs4 import BeautifulSoup
from oscpy.client import OSCClient
from oscpy.server import OSCThreadServer


state_request_hz = 10  # Only used to check if request for full state should be sent
ss_instance = None


def state_update_handler(*values):
    update_type = values[0]
    update_id = values[1]
    update_data = values[2:]
    if ss_instance is not None:
        ss_instance.apply_update(update_id, update_type, update_data)
    

def full_state_handler(*values):
    update_id = values[0]
    new_state_raw = values[1]
    if ss_instance is not None:
        ss_instance.set_full_state(update_id, new_state_raw)


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
    

class OSCReceiverThread(threading.Thread):

    def __init__(self, port, state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.state_synchronizer = state_synchronizer

    def run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        osc = OSCThreadServer()
        if ss_instance.verbose_level >= 1:
            print('* Listening OSC messages in port {}'.format(self.port))
        osc.listen(address='0.0.0.0', port=self.port, default=True)
        osc.bind(b'/app_started', lambda: ss_instance.app_has_started())
        osc.bind(b'/state_update', osc_state_update_handler)
        osc.bind(b'/full_state', osc_full_state_handler)
        osc.bind(b'/alive', lambda: ss_instance.app_is_alive())


def ws_on_message(ws, message):
    if ss_instance is not None:
        ss_instance.ws_connection_ok = True

    address = message[:message.find(':')]
    data = message[message.find(':') + 1:]

    if address == '/app_started':
        ss_instance.app_has_started()

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

    elif address == '/alive':
        # When using WS communication we don't need the /alive message to know the connection is alive as WS manages that
        pass


def ws_on_error(ws, error):
    if ss_instance is not None:
        if ss_instance.verbose_level >= 1:
            print("* WS connection error: {}".format(error))
        if 'Connection refused' not in str(error) and 'WebSocketConnectionClosedException' not in str(error):
            print(traceback.format_exc())
        ss_instance.ws_connection_ok = False


def ws_on_close(ws, close_status_code, close_msg):
    if ss_instance is not None:
        if ss_instance.verbose_level >= 1:
            print("* WS connection closed: {} - {}".format(close_status_code, close_msg))
        ss_instance.ws_connection_ok = False
        ss_instance.app_connection_lost()


def ws_on_open(ws):
    if ss_instance is not None:
        if ss_instance.verbose_level >= 1:
            print("* WS connection opened")
        ss_instance.ws_connection_ok = True
        ss_instance.app_has_started()


class WSConnectionThread(threading.Thread):

    reconnect_interval = 2
    last_time_tried_wss = True  # Start trying ws connection (instead of wss)

    def __init__(self, port, state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.state_synchronizer = state_synchronizer

    def run(self):
        # Try to establish a connection with the websockets server
        # If it can't be established, tries again every self.reconnect_interval seconds
        # Because we don't know if the server will use wss or not, we alternatively try
        # one or the other
        asyncio.set_event_loop(asyncio.new_event_loop())
        while True:
            try_wss = False #not self.last_time_tried_wss  # Force use of WS
            self.last_time_tried_wss = not self.last_time_tried_wss
            ws_endpoint = "ws{}://localhost:{}/shepherd_coms/".format('s' if try_wss else '', self.port)
            ws = websocket.WebSocketApp(ws_endpoint,
                                    on_open=ws_on_open,
                                    on_message=ws_on_message,
                                    on_error=ws_on_error,
                                    on_close=ws_on_close)
            self.state_synchronizer.ws_connection = ws
            if self.state_synchronizer.verbose_level >= 1:
                print('* Connecting to WS server: {}'.format(ws_endpoint))
            ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE}, skip_utf8_validation=True)
            if self.state_synchronizer.verbose_level >= 1:
                print('WS connection lost - will try connecting again in {} seconds'.format(self.reconnect_interval))
            time.sleep(self.reconnect_interval)
    

class RequestStateThread(threading.Thread):

    def __init__(self, state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.state_synchronizer = state_synchronizer

    def run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        if self.state_synchronizer.verbose_level >= 1:
            print('* Starting loop to request state')
        while True:
            time.sleep(1.0/state_request_hz)
            if self.state_synchronizer.should_request_full_state:
                self.state_synchronizer.request_full_state()


class StateSynchronizer(object):

    osc_client = None
    last_time_app_alive = 0  # Only used when using OSC communication

    ws_connection = None
    ws_connection_ok = False

    last_update_id = -1
    should_request_full_state = False
    full_state_requested = False
    last_time_full_state_requested = 0
    full_state_request_timeout = 5  # Seconds

    state_soup = None
    app = None

    use_websockets = True
    verbose_level = None

    def __init__(self,
                 osc_ip=None,
                 osc_port_send=None,
                 osc_port_receive=None,
                 ws_port=8126,
                 verbose_level=1):

        global ss_instance
        ss_instance = self
        self.verbose_level = verbose_level

        if (osc_port_receive is None or osc_port_send is None or osc_ip is None) is None and ws_port is None:
            raise Exception('OSC/WS ports are not properly configured')

        if ws_port is None:
            self.use_websockets = False

        if not self.use_websockets:
            if self.verbose_level >= 1:
                print('* Using OSC to communicate with app')

            # Start OSC receiver to receive OSC messages from the app
            OSCReceiverThread(osc_port_receive, self).start()
            
            # Start OSC client to send OSC messages to app
            self.osc_client = OSCClient(osc_ip, osc_port_send, encoding='utf8')
            if self.verbose_level >= 1:
                print('* Sending OSC messages in port {}'.format(osc_port_send))
        else:
            if self.verbose_level >= 1:
                print('* Using WebSockets to communicate with app')

            # Start websockets client to handle communication with app
        WSConnectionThread(ws_port, self).start()

        # Start Thread to request state to app
        # This thread will request the full state if self.should_request_full_state is set to True
        # (which by default is false)
        RequestStateThread(self).start()
        self.should_request_full_state = True

    def send_msg_to_app(self, address, values):
        if self.osc_client is not None:
            self.osc_client.send_message(address, values)
        if self.ws_connection is not None and self.ws_connection_ok:
            self.ws_connection.send(address + ':' + ';'.join([str(value) for value in values]))

    def app_has_started(self):
        self.last_update_id = -1
        self.full_state_requested = False
        self.should_request_full_state = True

    def app_connection_lost(self):
        # Maybe subclasses want to do something with that...
        pass

    def app_is_alive(self):
        self.last_time_app_alive = time.time()

    def app_may_be_down(self):
        if not self.use_websockets:
            return time.time() - self.last_time_app_alive > 5.0  # Consider app maybe down if no alive message in 5 seconds
        else:
            return not self.ws_connection_ok  # Consider app down if no active WS connection

    def request_full_state(self):
        if ((time.time() - self.last_time_full_state_requested) > self.full_state_request_timeout):
            # If full state has not returned for a while, ask again
            self.full_state_requested = False

        if not self.full_state_requested and not self.app_may_be_down():
            if self.verbose_level >= 2:
                print('* Requesting full state')
            self.full_state_requested = True
            self.last_time_full_state_requested = time.time()
            self.send_msg_to_app('/get_state', ["full"])

    def set_full_state(self, update_id, full_state_raw):
        if self.verbose_level >= 2:
            print("Receiving full state with update id {}".format(update_id))
        full_state_soup = BeautifulSoup(full_state_raw, "lxml")
        self.full_state_requested = False
        self.should_request_full_state = False
        self.on_full_state_received(full_state_soup)

    def apply_update(self, update_id, update_type, update_data):
        if self.verbose_level >= 2:
            print("Applying state update {} - {}".format(update_id, update_type))
        self.on_state_update(update_type, update_data)

        # Check if update ID is correct and trigger request of full state if there are possible sync errors
        if self.last_update_id != -1 and self.last_update_id + 1 != update_id:
            if self.verbose_level >= 2:
                print('WARNING: last_update_id does not match with received update ({} vs {})'
                      .format(self.last_update_id + 1, update_id))
            self.should_request_full_state = True
        self.last_update_id = update_id
    
    def on_state_update(self, update_type, update_data):
        pass

    def on_full_state_received(self, full_state_soup):
        pass

