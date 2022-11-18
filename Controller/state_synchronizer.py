from oscpy.client import OSCClient
from oscpy.server import OSCThreadServer
import threading
import asyncio
from bs4 import BeautifulSoup
import time
import sys
import ssl
import traceback

# If USE_WEBSOCKETS is set to True, WebSockets will be used to communicate with 
# app, otherwise OSC will be used
USE_WEBSOCKETS = True

# If USE_STATE_DEBUGGER is set to True, current synchronized state will be available 
# through a localchost server listening at 'state_debugger_port'
USE_STATE_DEBUGGER = True

osc_send_host = "127.0.0.1"
osc_send_port = 9003
osc_receive_port = 9004
ws_port = 8125 
state_debugger_port = 5100
state_request_hz = 10 

sss_instance = None
state_debugger_server = None
state_debugger_autoreload_ms = 1000


if USE_WEBSOCKETS:
    import websocket

if USE_STATE_DEBUGGER:
    from flask import Flask, render_template
    state_debugger_server = Flask(__name__)

    import logging
    disable_flask_logging = True
    if disable_flask_logging:
        log = logging.getLogger('werkzeug')
        log.setLevel(logging.ERROR)

    @state_debugger_server.route('/raw')
    def state_debugger_raw():
        if sss_instance is None or sss_instance.state_soup is None:
            state = 'No state has been synced yet'
        else:
            state = sss_instance.state_soup.findAll("session")[0]
        return render_template('state_debugger.html', state=state, xml=True, state_debugger_autoreload_ms=state_debugger_autoreload_ms)

    @state_debugger_server.route('/python')
    def state_debugger_python():
        if sss_instance is None or sss_instance.state_soup is None:
            state = 'No state has been synced yet'
        else:
            state = sss_instance.session.render_session(include_attributes=True)
        return render_template('state_debugger.html', state=state, xml=False, state_debugger_autoreload_ms=state_debugger_autoreload_ms)


class StateDebuggerServerThread(threading.Thread):

    def __init__(self, port, state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port
        self.state_synchronizer = state_synchronizer

    def run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        print('* Starting state debugger in port {}'.format(self.port))
        state_debugger_server.run(host='0.0.0.0', port=self.port, debug=True, use_reloader=False)


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
        print('* Listening OSC messages in port {}'.format(self.port))
        osc.listen(address='0.0.0.0', port=self.port, default=True)
        osc.bind(b'/app_started', lambda: sss_instance.app_has_started())
        osc.bind(b'/state_update', osc_state_update_handler)
        osc.bind(b'/full_state', osc_full_state_handler)
        osc.bind(b'/alive', lambda: sss_instance.app_is_alive())
        osc.bind(b'/midiCCParameterValuesForDevice', lambda *values: sss_instance.app.shepherd_interface.receive_midi_cc_values_for_device(*values))
    

def ws_on_message(ws, message):
    if sss_instance is not None:
        sss_instance.ws_connection_ok = True

    address = message[:message.find(':')]
    data = message[message.find(':') + 1:]
    
    if address == '/app_started':
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

    elif address == '/alive':
        # When using WS communication we don't need the /alive message to know the connection is alive as WS manages that
        pass

    elif address == '/midiCCParameterValuesForDevice':
        # This is used for the midi CC mode so that it knows the current values of each of the encoders
        data_values = data.split(';')
        sss_instance.app.shepherd_interface.receive_midi_cc_values_for_device(*data_values)


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
        sss_instance.app_has_started()


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
            print('* Connecting to WS server: {}'.format(ws_endpoint))
            ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE}, skip_utf8_validation=True)
            print('WS connection lost - will try connecting again in {} seconds'.format(self.reconnect_interval))
            time.sleep(self.reconnect_interval)
    

class RequestStateThread(threading.Thread):

    def __init__(self, state_synchronizer, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.state_synchronizer = state_synchronizer

    def run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        print('* Starting loop to request state')
        while True:
            time.sleep(1.0/state_request_hz)
            if self.state_synchronizer.should_request_full_state:
                self.state_synchronizer.request_full_state()


class GenericStateSynchronizer(object):

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

    verbose = False

    def __init__(self, 
                 app, 
                 osc_ip=osc_send_host, 
                 osc_port_send=osc_send_port, 
                 osc_port_receive=osc_receive_port, 
                 ws_port=ws_port, 
                 state_debugger_port=state_debugger_port, 
                 verbose=True):
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
        # This thread will request the full state if self.should_request_full_state is set to True
        RequestStateThread(self).start()

        # If state debugger is enabled, start  the server
        if state_debugger_server is not None:
            StateDebuggerServerThread(state_debugger_port, self).start()

        time.sleep(0.5)  # Give time for threads to start
        self.should_request_full_state = True

    def send_msg_to_app(self, address, values):
        if self.osc_client is not None:
            self.osc_client.send_message(address, values)
        if self.ws_connection is not None and self.ws_connection_ok:
            self.ws_connection.send(address + ':' + ';'.join([str(value) for value in values]))

    def app_has_started(self):
        self.last_update_id = -1
        self.state_soup = None
        self.full_state_requested = False
        self.should_request_full_state = True

    def app_is_alive(self):
        self.last_time_app_alive = time.time()

    def app_may_be_down(self):
        if not USE_WEBSOCKETS:
            return time.time() - self.last_time_app_alive > 5.0  # Consider app maybe down if no alive message in 5 seconds
        else:
            return not self.ws_connection_ok  # Consider app down if no active WS connection

    def request_full_state(self):
        if ((time.time() - self.last_time_full_state_requested) > self.full_state_request_timeout):
            # If full state has not returned for a while, ask again
            self.full_state_requested = False

        if not self.full_state_requested and not self.app_may_be_down():
            print('* Requesting full state')
            self.full_state_requested = True
            self.last_time_full_state_requested = time.time()
            self.send_msg_to_app('/get_state', ["full"])

    def set_full_state(self, update_id, full_state_raw):
        if self.verbose:
            print("Receiving full state with update id {}".format(update_id))
        self.state_soup = BeautifulSoup(full_state_raw, "lxml")
        self.full_state_requested = False
        self.should_request_full_state = False

        # Call method for possible callbacks after state is updated
        self.on_state_update()

        # Call method after ful state is received
        self.on_full_state_received()

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

                    # Call method for possible callbacks after state is updated
                    self.on_state_update(soup_object=results[0], attribute_changed=property_name)
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

                    # Call method for possible callbacks after state is updated
                    self.on_state_update()
                    
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
                    
                    # Call method for possible callbacks after state is updated
                    self.on_state_update()
                else:
                    # Should never return more than one, request a full state as there will be sync issues
                    if self.verbose:
                        print('Unexpected number of results ({})'.format(len(results)))
                    self.should_request_full_state = True
            
            # Check if update ID is correct and trigger request of full state if there are possible sync errors
            if self.last_update_id != -1 and self.last_update_id + 1 != update_id:
                self.should_request_full_state = True
            self.last_update_id = update_id

            

    def on_state_update(self, soup_object=None):
        pass

    def on_full_state_received(self):
        pass


# ----------------

parameters_types = {
    'barcount': int,
    'bpm': float,
    'cliplengthinbeats': float,
    'countinplayheadpositioninbeats': float,
    'currentquantizationstep': float,
    'doingcountin': bool,
    'doingcountin': bool, 
    'duration': float,
    'enabled': bool,
    'fixedlengthrecordingbars': int,    
    'fixedvelocity': bool,
    'hardwaredevicename': str,
    'inputmonitoring': bool,
    'isplaying': bool,
    'meter': int,
    'metronomeon': bool, 
    'midinote':int, 
    'midivelocity': float,  # . 0.0-1.0
    'name': str,
    'notesmonitoringdevicename': str,
    'order': int,
    'playheadpositioninbeats': float,
    'playing': bool,
    'recordautomationenabled': bool,
    'recording': bool,
    'renderedendtimestamp': float,
    'renderedstarttimestamp': float,
    'timestamp': float,
    'type': int, # SequenceEventType {midi, note}
    'uuid': str,
    'willplayat': float,
    'willstartrecordingat': float,
    'willstopat': float,
    'willstoprecordingat': float,
    'wrapeventsacrosscliploop': bool,
}


def backend_value_to_python_value(attr_name, value):
    attr_type = parameters_types.get(attr_name, None)
    try:
        if attr_type == float:
            return float(value)
        elif attr_type == int:
            return int(value)
        elif attr_type == bool:
            return value == '1'
        elif attr_type == str:
            return value
        elif attr_type == None:
            print('WARNING: unknown parameter {} of received type {}'.format(attr_name, type(value)))
            return value
    except Exception as e:
        raise Exception('Error converting value {} for attribute {}: {}'.format(value, attr_name, str(e)))


def python_value_to_backend_value(attr_name, value):
    # TODO: is htis needed?
    attr_type = parameters_types.get(attr_name, None)
    try:
        if attr_type == float:
            return str(value)
        elif attr_type == int:
            return str(value)
        elif attr_type == bool:
            return '1' if value else '0'
        elif attr_type == str:
            return value
        elif attr_type == None:
            print('WARNING: don\' know how to python_value_to_backend_value parameter {} of received value type {}'.format(attr_name, type(value)))
            return value
    except Exception as e:
        raise Exception('Error converting value {} for attribute {}: {}'.format(value, attr_name, str(e)))


class BaseShepherdClass(object):

    def __init__(self, soup, state_synchronizer):
        # Set state syncronizer (will be used to be able to send messages to backend)
        self.state_synchronizer = state_synchronizer

        # Set initial attributes and methods to set these attributes in the backend
        for attr_name, value in soup.attrs.items():
            setattr(self, attr_name, backend_value_to_python_value(attr_name, value))
    
    def send_msg_to_app(self, address, values):
        self.state_synchronizer.send_msg_to_app(address, values)


class Session(BaseShepherdClass):
    tracks = []

    def __init__(self, *args, **kwargs):
        self.tracks = []
        super().__init__(*args, **kwargs)

    def add_track(self, track):
        self.tracks.append(track)

    def render_object_attributes(self, obj, num_spaces_offset=0):
        text = ''
        for attr_name in dir(obj):
            if not attr_name.startswith('_') and not callable(getattr(obj, attr_name)) and not type(getattr(obj, attr_name)) == list and attr_name != 'state_synchronizer' and attr_name != 'track':
                text += '{}{}: {}\n'.format(' ' * num_spaces_offset, attr_name, getattr(obj, attr_name))
        return text

    def render_session(self, include_attributes=False):
        text = '------------------------------------------------\n'
        text += 'SESSION {} ({})\n'.format(self.name, self.uuid)
        if include_attributes:
            text += self.render_object_attributes(self)
        for track in self.tracks:
            text += '  * TRACK {} ({})\n'.format(track.name, track.uuid)
            if include_attributes:
                text += self.render_object_attributes(track, num_spaces_offset=4)
            for clip in track.clips:
                text += '    * CLIP {} ({}) {}\n'.format(clip.name, clip.uuid, clip.get_status())
                if include_attributes:
                    text += self.render_object_attributes(clip, num_spaces_offset=6)
                for sequence_event in clip.sequence_events:
                    text += '      * SEQUENCE_EVENT {}\n'.format(sequence_event.uuid)
                    if include_attributes:
                        text += self.render_object_attributes(sequence_event, num_spaces_offset=8)
        return text

    def pprint(self, include_attributes=False):
        print(self.render_session(include_attributes=include_attributes))

    def scene_play(self, scene_number):
        self.send_msg_to_app('/scene/play', [scene_number])

    def scene_duplicate(self, scene_number):
        self.send_msg_to_app('/scene/duplicate', [scene_number])

    def global_play_stop(self):
        self.send_msg_to_app('/transport/playStop', [])

    def metronome_on_off(self):
        self.send_msg_to_app('/metronome/onOff', [])

    def set_bpm(self, new_bpm):
        self.send_msg_to_app('/transport/setBpm', [new_bpm])
    
    def set_meter(self, new_meter):
        self.send_msg_to_app('/transport/setMeter', [new_meter])

    def set_fix_length_recording_bars(self, new_fixed_length_recording_bars):
        self.send_msg_to_app('/settings/fixedLength', [new_fixed_length_recording_bars])

    def set_record_automation_on_off(self):
        self.send_msg_to_app('/settings/toggleRecordAutomation', [])
        

class Track(BaseShepherdClass):
    clips = []
    
    def __init__(self, *args, **kwargs):
        self.clips = []
        super().__init__(*args, **kwargs)

    def add_clip(self, clip):
        self.clips.append(clip)

    def set_input_monitoring(self, enabled):
        self.send_msg_to_app('/track/setInputMonitoring', [self.uuid, 1 if enabled else 0])

    def set_active_ui_notes_monitoring(self):
        self.send_msg_to_app('/track/setActiveUiNotesMonitoringTrack', [self.uuid])


class Clip(BaseShepherdClass):
    sequence_events = []

    def __init__(self, *args, **kwargs):
        self.sequence_events = []
        self.track = kwargs['owning_track']  # Save a reference to the owning track
        del kwargs['owning_track']
        super().__init__(*args, **kwargs)

    def add_sequence_event(self, sequence_event):
        self.sequence_events.append(sequence_event)

    def play_stop(self):
        self.send_msg_to_app('/clip/playStop', [self.track.uuid, self.uuid])

    def get_status(self):
        CLIP_STATUS_PLAYING = "p"
        CLIP_STATUS_STOPPED = "s"
        CLIP_STATUS_CUED_TO_PLAY = "c"
        CLIP_STATUS_CUED_TO_STOP = "C"
        CLIP_STATUS_RECORDING = "r"
        CLIP_STATUS_CUED_TO_RECORD = "w"
        CLIP_STATUS_CUED_TO_STOP_RECORDING = "W"
        CLIP_STATUS_NO_RECORDING = "n"
        CLIP_STATUS_IS_EMPTY = "E"
        CLIP_STATUS_IS_NOT_EMPTY = "e"

        if self.willstartrecordingat >= 0.0:
            record_status = CLIP_STATUS_CUED_TO_RECORD
        elif self.willstoprecordingat >= 0.0:
            record_status = CLIP_STATUS_CUED_TO_STOP_RECORDING
        elif self.recording:
            record_status = CLIP_STATUS_RECORDING
        else:
            record_status = CLIP_STATUS_NO_RECORDING

        if self.willplayat >= 0.0:
            play_status = CLIP_STATUS_CUED_TO_PLAY
        elif self.willstopat >= 0.0:
            play_status = CLIP_STATUS_CUED_TO_STOP
        elif self.playing:
            play_status = CLIP_STATUS_PLAYING
        else:
            play_status = CLIP_STATUS_STOPPED
    
        if self.cliplengthinbeats == 0.0:
            empty_status = CLIP_STATUS_IS_EMPTY
        else:
            empty_status = CLIP_STATUS_IS_NOT_EMPTY
        return f'{play_status}{record_status}{empty_status}|{self.cliplengthinbeats:.3f}|{self.currentquantizationstep}'

    def is_empty(self):
        return 'E' in self.get_status()

    def record_on_off(self):
        self.send_msg_to_app('/clip/recordOnOff', [self.track.uuid, self.uuid])

    def clear(self):
        self.send_msg_to_app('/clip/clear', [self.track.uuid, self.uuid])

    def double(self):
        self.send_msg_to_app('/clip/double', [self.track.uuid, self.uuid])

    def quantize(self, quantization_step):
        self.send_msg_to_app('/clip/quantize', [self.track.uuid, self.uuid, quantization_step])

    def undo(self):
        self.send_msg_to_app('/clip/undo', [self.track.uuid, self.uuid])

    def set_length(self, new_length):
        self.send_msg_to_app('/clip/setLength', [self.track.uuid, self.uuid, new_length])


class SequenceEvent(BaseShepherdClass):
    pass


class ShepherdStateSynchronizer(GenericStateSynchronizer):

    session = None
    elements_uuids_map = {}
    showing_countin_message = False  # This should be removed from here and move to somewhere in app/shepherd_interface (see on_state_update below)

    def add_element_to_uuid_map(self, element):
        self.elements_uuids_map[element.uuid] = element

    def get_element_with_uuid(self, uuid):
        return self.elements_uuids_map[uuid]

    def on_state_update(self, soup_object=None, attribute_changed=None):

        if soup_object is not None:
            # If a soup object is passed, then only update the attributes of that object (which should exist in the current session tree)
            session_tree_element = self.get_element_with_uuid(soup_object['uuid'])
            if hasattr(session_tree_element, attribute_changed):
                setattr(session_tree_element, attribute_changed, backend_value_to_python_value(attribute_changed, soup_object[attribute_changed]))
            else:
                raise Exception('Trying to update state value for an attribute that does not exist ({} of object {})'.format(attribute_changed, session_tree_element.uuid))
            # If we are trying to update an attribute that does not exist (or an element that does not exist), this will raise an error
            # This is to be expected as we should never get here trying to update an attribute/element that does not exist
        else:
            # If no soup object is passed, then rebuild the whole session tree
            self.build_session()

        # TODO: the code below is app-specific code that should not be implemented here but with some sort of property listener somehwere in app or shepherd interface
        if attribute_changed == 'playheadpositioninbeats' or attribute_changed == 'countinplayheadpositioninbeats':
            if self.session.doingcountin:
                self.showing_countin_message = True
                import math
                self.app.add_display_notification("Will start recording in: {0:.0f}".format(math.ceil(4 - self.session.countinplayheadpositioninbeats)))
            else:
                if self.showing_countin_message:
                    self.app.clear_display_notification()
                    self.showing_countin_message = False

        # Trigger re-activation of modes in case pads need to be updated
        # TODO: this should be optimized, and the different modes should "subscribe" to object changes so they know when they need to update
        if self.app.shepherd_interface is not None:
            self.app.shepherd_interface.reactivate_modes()

    def on_full_state_received(self):
        self.build_session()

        # Trigger re-activation of modes in case pads need to be updated
        if self.app.shepherd_interface is not None:
            self.app.shepherd_interface.receive_shepherd_ready()

    def build_session(self):
        self.elements_uuids_map = {}
        session_soup = self.state_soup.findAll("session")[0]
        session = Session(session_soup, self)
        self.add_element_to_uuid_map(session)
        for track_soup in session_soup.findAll("track"):
            track = Track(track_soup, self)
            self.add_element_to_uuid_map(track)
            for count, clip_soup in enumerate(track_soup.findAll("clip")):
                clip = Clip(clip_soup, self, owning_track=track)
                self.add_element_to_uuid_map(clip)
                for sequence_event_soup in clip_soup.findAll("sequence_event"):
                    sequence_event = SequenceEvent(sequence_event_soup, self)
                    clip.add_sequence_event(sequence_event)
                    self.add_element_to_uuid_map(sequence_event)
                track.add_clip(clip)
            session.add_track(track)
        self.session = session