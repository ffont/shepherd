import json
import math
import mido

from bs4 import BeautifulSoup

from .state_synchronizer import StateSynchronizer
from .state_debugger import start_state_debugger

parameters_types = {
    'availablehardwaredevicenames': str,
    'barcount': int,
    'bpm': float,
    'chance': float,
    'cliplengthinbeats': float,
    'countinplayheadpositioninbeats': float,
    'currentquantizationstep': float,
    'datalocation': str,
    'doingcountin': bool,
    'duration': float,
    'eventmidibytes': str,
    'fixedlengthrecordingbars': int,    
    'fixedvelocity': bool,
    'hardwaredevicename': str,
    'inputmonitoring': bool,
    'isplaying': bool,
    'meter': int,
    'metronomeon': bool,
    'midiccparametervalueslist': str,
    'midichannel': int,
    'mididevicename': str,
    'midinote': int,
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
    'renderwithinternalsynth': bool,
    'shortname': str,
    'timestamp': float,
    'type': int,  # SequenceEventType {midi=0, note=1} or HardwareDeviceType {input=0, output=1}
    'utime': float,
    'uuid': str,
    'version': str,
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
        elif attr_type is None:
            print('WARNING: unknown parameter {} of received type {}'.format(attr_name, type(value)))
            return value
    except Exception as e:
        raise Exception('Error converting value {} for attribute {}: {}'.format(value, attr_name, str(e)))


class BaseShepherdClass(object):

    parent = None

    def __init__(self, soup, shepherd_backend_interface, parent=None):
        # Set parent
        self.parent = parent

        # Set state synchronizer (will be used to be able to send messages to backend)
        self.shepherd_backend_interface = shepherd_backend_interface

        # Set initial attributes and methods to set these attributes in the backend
        for attr_name, value in soup.attrs.items():
            setattr(self, attr_name, backend_value_to_python_value(attr_name, value))
    
    def send_msg_to_app(self, address, values):
        self.shepherd_backend_interface.send_msg_to_app(address, values)

    def render_object_attributes(self, num_spaces_offset=0):
        text = ''
        for attr_name in dir(self):
            if not attr_name.startswith('_') \
                    and not callable(getattr(self, attr_name)) \
                    and not type(getattr(self, attr_name)) == list \
                    and attr_name != 'shepherd_backend_interface' \
                    and attr_name != 'parent' \
                    and attr_name != 'track' \
                    and attr_name != 'clip' \
                    and attr_name != 'state' \
                    and attr_name != 'session':
                text += '{}{}: {}\n'.format(' ' * num_spaces_offset, attr_name, getattr(self, attr_name))
        return text


class State(BaseShepherdClass):
    hardware_devices = []
    session = None

    def __init__(self, *args, **kwargs):
        self.hardware_devices = []
        super().__init__(*args, **kwargs)

    def add_hardware_device(self, hardware_device, position=None):
        # Note this method adds a HardwareDevice object in the local State state object but does not create a hardware
        # device in the backend
        if position is None:
            self.hardware_devices.append(hardware_device)
        else:
            self.hardware_devices.insert(position, hardware_device)

    def remove_hardware_device_with_uuid(self, hardware_device_uuid):
        # Note this method removes a HardwareDevice object from the local State state object but does not remove a
        # hardware device from the backend
        self.hardware_devices = [hardware_device for hardware_device in self.hardware_devices
                                 if hardware_device.uuid != hardware_device_uuid]

    def get_hardware_device_by_name(self, hardware_device_name):
        for hardware_device in self.hardware_devices:
            if hardware_device.name == hardware_device_name or hardware_device.shortname == hardware_device_name:
                return hardware_device
        return None

    def render(self, include_attributes=False):
        text = 'SHEPHERD BACKEND STATE\n'
        if include_attributes:
            text += self.render_object_attributes()

        if self.session is not None:
            text += '* SESSION {} ({})\n'.format(self.session.name, self.session.uuid)
            if include_attributes:
                text += self.session.render_object_attributes(num_spaces_offset=2)
            for track in self.session.tracks:
                text += '  * TRACK {} ({})\n'.format(track.name, track.uuid)
                if include_attributes:
                    text += track.render_object_attributes(num_spaces_offset=4)
                for clip in track.clips:
                    text += '    * CLIP {} ({}) {}\n'.format(clip.name, clip.uuid, clip.get_status())
                    if include_attributes:
                        text += clip.render_object_attributes(num_spaces_offset=6)
                    for sequence_event in clip.sequence_events:
                        text += '      * SEQUENCE_EVENT {}\n'.format(sequence_event.uuid)
                        if include_attributes:
                            text += sequence_event.render_object_attributes(num_spaces_offset=8)

        if self.hardware_devices:
            text += '* HARDWARE DEVICES ({})\n'.format(len(self.hardware_devices))
            for hardware_device in self.hardware_devices:
                text += '  * HARDWARE_DEVICE {} ({})\n'.format(hardware_device.name, hardware_device.uuid)
                if include_attributes:
                    text += hardware_device.render_object_attributes(num_spaces_offset=4)
        return text

    def send_controller_ready_message_to_backend(self):
        self.send_msg_to_app('/shepherdControllerReady', [])
    
    def get_available_output_hardware_device_names(self):
        return [device.shortname for device in self.hardware_devices if device.is_type_output()]

    def toggle_shepherd_backend_debug_synth(self):
        self.send_msg_to_app('/settings/debugSynthOnOff', [])

    def set_push_pads_mapping(self, new_mapping=[]):
        if new_mapping:
            self.send_msg_to_app('/settings/pushNotesMapping', new_mapping)

    def set_push_encoders_mapping(self, device_name, new_mapping=[]):
        if device_name == "":
            device_name = "-"
        if new_mapping:
            self.send_msg_to_app('/settings/pushEncodersMapping', [device_name] + new_mapping)


class Session(BaseShepherdClass):
    tracks = []

    @property
    def state(self):
        return self.parent

    def __init__(self, *args, **kwargs):
        self.tracks = []
        super().__init__(*args, **kwargs)

    def add_track(self, track, position=None):
        # Note this method adds a Track object in the local Session object but does not create a track in the backend
        if position is None:
            self.tracks.append(track)
        else:
            self.tracks.insert(position, track)

    def remove_track_with_uuid(self, track_uuid):
        # Note this method removes a Track object from the local Session object but does not remove a track from
        # the backend
        self.tracks = [track for track in self.tracks if track.uuid != track_uuid]

    def get_track_by_idx(self, track_idx=None):
        try:
            return self.tracks[track_idx]
        except Exception as e:
            print('ERROR selecting track: {}'.format(e))
        return None

    def get_clip_by_idx(self, track_idx=None, clip_idx=None):
        try:
            return self.tracks[track_idx].clips[clip_idx]
        except Exception as e:
            print('ERROR selecting track: {}'.format(e))
        return None

    def save(self, save_session_name):
        self.send_msg_to_app('/settings/save', [save_session_name])

    def load(self, load_session_name):
        self.send_msg_to_app('/settings/load', [load_session_name])

    def new(self, num_tracks, num_scenes):
        self.send_msg_to_app('/settings/new', [num_tracks, num_scenes])

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

    def set_fixed_velocity(self, velocity):
        self.send_msg_to_app('/settings/fixedVelocity', [velocity])

    def set_record_automation_on_off(self):
        self.send_msg_to_app('/settings/toggleRecordAutomation', [])
        

class Track(BaseShepherdClass):
    clips = []

    @property
    def session(self):
        return self.parent
    
    def __init__(self, *args, **kwargs):
        self.clips = []
        super().__init__(*args, **kwargs)

    def add_clip(self, clip, position=None):
        # Note this method adds a Clip object in the local Trck object but does not create a clip in the backend
        if position is None:
            self.clips.append(clip)
        else:
            self.clips.insert(position, clip)

    def remove_clip_with_uuid(self, clip_uuid):
        # Note this method removes a Clip object from the local Track object but does not remove a clip from the backend
        self.clips = [clip for clip in self.clips if clip.uuid != clip_uuid]

    def get_hardware_device(self):
        return self.session.state.get_hardware_device_by_name(self.hardwaredevicename)

    def set_input_monitoring(self, enabled):
        self.send_msg_to_app('/track/setInputMonitoring', [self.uuid, 1 if enabled else 0])

    def set_active_ui_notes_monitoring(self):
        self.send_msg_to_app('/track/setActiveUiNotesMonitoringTrack', [self.uuid])

    def set_hardware_device(self, device_name):
        self.send_msg_to_app('/track/setHardwareDevice', [self.uuid, device_name])


class Clip(BaseShepherdClass):
    sequence_events = []

    @property
    def track(self):
        return self.parent

    def add_sequence_event(self, sequence_event, position=None):
        # Note this method adds a SequenceEvent object in the local Clip object but does not create a sequence event
        # in the backend
        if position is None:
            self.sequence_events.append(sequence_event)
        else:
            self.sequence_events.insert(position, sequence_event)

    def remove_sequence_event_with_uuid(self, sequence_event_uuid):
        # Note this method removes a SequenceEvent object from the local Clip object but does not remove a sequence
        # event from the backend
        self.sequence_events = [sequence_event for sequence_event in self.sequence_events
                                if sequence_event.uuid != sequence_event_uuid]

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

    def play_stop(self):
        self.send_msg_to_app('/clip/playStop', [self.track.uuid, self.uuid])
    
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

    def set_sequence(self, new_sequence):
        """new_sequence must be passed as a dictionary with this form:
        {
            "clipLength": 6,
            "sequenceEvents": [
                {"type": 1, "midiNote": 79, "midiVelocity": 1.0, "timestamp": 0.29, "duration": 0.65, ...},
                {"type": 1, "midiNote": 73, "midiVelocity": 1.0, "timestamp": 2.99, "duration": 1.42, ...},
                {"type": 0, "eventMidiBytes": "73,21,56", "timestamp": 2.99, ...},  # type 0 = generic midi message
                ...
            ]
        }
        """
        self.send_msg_to_app("/clip/setSequence", [self.track.uuid, self.uuid, json.dumps(new_sequence)])

    def edit_sequence(self, edit_sequence_data):
        """edit_sequence_data should be a dictionary with this form:
        {
            "action": "removeEvent" | "editEvent" | "addEvent",  // One of these three options
            "eventUUID":  "356cbbdjgf...", // Used by "removeEvent" and "editEvent" only
            "eventProperties": {
                "type": 1,
                "midiNote": 79,
                "midiVelocity": 1.0,
                ... // All the event properties that should be updated or "added" (in case of a new event)
        }
        Note that there are more specialized methods that will call "edit_sequence" and will have easier interface
        """
        self.send_msg_to_app("/clip/editSequence", [self.track.uuid, self.uuid, json.dumps(edit_sequence_data)])

    def remove_sequence_event(self, event_uuid):
        self.edit_sequence({
            'action': 'removeEvent',
            'eventUUID': event_uuid, 
        })

    def add_sequence_note_event(self, midi_note, midi_velocity, timestamp, duration, utime=0.0, chance=1.0):
        self.edit_sequence({
            'action': 'addEvent',
            'eventData': {
                'type': 1,  # type 1 = note event
                'midiNote': midi_note, 
                'midiVelocity': midi_velocity,  # 0.0 to 1.0 
                'timestamp': timestamp, 
                'duration': duration,
                'chance': chance,
                'utime': utime
            }, 
        })

    def add_sequence_midi_event(self, eventMidiBytes, timestamp):
        self.edit_sequence({
            'action': 'addEvent',
            'eventData': {
                'type': 0,  # type 0 = midi event
                'eventMidiBytes': eventMidiBytes,
                'timestamp': timestamp, 
            }, 
        })

    def edit_sequence_event(self, event_uuid, midi_note=None, midi_velocity=None, timestamp=None, duration=None,
                            midi_bytes=None, utime=None, chance=None):
        event_data = {}
        if midi_note is not None:
            event_data['midiNote'] = midi_note
        if midi_velocity is not None:
            event_data['midiVelocity'] = midi_velocity
        if timestamp is not None:
            event_data['timestamp'] = timestamp
        if duration is not None:
            event_data['duration'] = duration
        if midi_bytes is not None:
            event_data['eventMidiBytes'] = midi_bytes
        if utime is not None:
            event_data['utime'] = utime
        if chance is not None:
            event_data['chance'] = chance
        self.edit_sequence({
            'action': 'editEvent',
            'eventUUID': event_uuid,
            'eventData': event_data, 
        })


class SequenceEvent(BaseShepherdClass):

    @property
    def clip(self):
        return self.parent

    def is_type_note(self):
        return self.type == 1

    def is_type_midi(self):
        return self.type == 0

    def set_timestamp(self, timestamp):
        self.clip.edit_sequence_event(self.uuid, timestamp=timestamp)

    def set_utime(self, utime):
        self.clip.edit_sequence_event(self.uuid, utime=utime)

    def set_midi_note(self, midi_note):
        if self.is_type_note():
            self.clip.edit_sequence_event(self.uuid, midi_note=midi_note)

    def set_midi_velocity(self, midi_velocity):
        if self.is_type_note():
            self.clip.edit_sequence_event(self.uuid, midi_velocity=midi_velocity)

    def set_duration(self, duration):
        if self.is_type_note():
            self.clip.edit_sequence_event(self.uuid, duration=duration)

    def set_chance(self, chance):
        if self.is_type_note():
            self.clip.edit_sequence_event(self.uuid, chance=chance)

    def set_midibytes(self, midi_bytes):
        if self.is_type_midi():
            self.clip.edit_sequence_event(self.uuid, midi_bytes=midi_bytes)


class HardwareDevice(BaseShepherdClass):

    _midiccparametervalueslist_used_for_splitting = None
    _midiccparametervalueslist_splitted = []
    
    def is_type_output(self):
        return self.type == 1

    def is_type_input(self):
        return self.type == 0

    def send_midi(self, msg: mido.Message):
        self.send_msg_to_app('/device/sendMidi', [self.name] + msg.bytes())

    def all_notes_off(self):
        self.send_msg_to_app('/device/sendAllNotesOff', [self.name])

    def load_preset(self, bank, preset):
        self.send_msg_to_app('/device/loadDevicePreset', [self.name, bank, preset])

    def get_current_midi_cc_parameter_value(self, midi_cc_num):
        if self.midiccparametervalueslist != self._midiccparametervalueslist_used_for_splitting:
            self._midiccparametervalueslist_used_for_splitting = self.midiccparametervalueslist
            self._midiccparametervalueslist_splitted = self.midiccparametervalueslist.split(',')
        return int(self._midiccparametervalueslist_splitted[midi_cc_num])


class ShepherdBackendInterface(StateSynchronizer):

    state = None
    elements_uuids_map = {}
    showing_countin_message = False  # This should be removed from here and move to somewhere in app/shepherd_interface

    def __init__(self, *args, **kwargs):
        debugger_port = kwargs.get('debugger_port', None)
        try:
            del kwargs['debugger_port']
        except:
            pass
        super().__init__(*args, **kwargs)
        if debugger_port is not None:
            start_state_debugger(self, debugger_port)

    def add_element_to_uuid_map(self, element):
        self.elements_uuids_map[element.uuid] = element

    def remove_element_from_uuid_map(self, uuid):
        del self.elements_uuids_map[uuid]

    def get_element_with_uuid(self, uuid):
        return self.elements_uuids_map[uuid]

    def on_state_update(self, update_type, update_data):

        if self.state is None:
            # If we don't have a session built, request new full state and ignore current state update
            self.should_request_full_state = True
        else:
            if update_type == "propertyChanged":
                tree_uuid = update_data[0]
                try:
                    tree_element = self.get_element_with_uuid(tree_uuid)
                    property_name = update_data[2].lower()
                    new_value = update_data[3]
                    if hasattr(tree_element, property_name):
                        setattr(tree_element, property_name, backend_value_to_python_value(property_name, new_value))
                    else:
                        raise Exception('Trying to update state value for an attribute that does not exist '
                                        '({} of object {})'.format(property_name, tree_element.uuid))
                    # If we are trying to update an attribute that does not exist (or an element that does not exist),
                    # this will raise an error. This is to be expected as we should never get here trying to update
                    # an attribute/element that does not exist

                    # TODO: the code below is app-specific code that should not be implemented here but with some sort
                    #  of property listener somehwere in app or shepherd interface
                    if property_name == 'playheadpositioninbeats' or property_name == 'countinplayheadpositioninbeats':
                        if self.state.session.doingcountin:
                            self.showing_countin_message = True
                            self.app.add_display_notification(
                                "Will start recording in: {0:.0f}"
                                    .format(math.ceil(4 - self.state.session.countinplayheadpositioninbeats)))
                        else:
                            if self.showing_countin_message:
                                self.app.clear_display_notification()
                                self.showing_countin_message = False

                except KeyError as e:
                    print('WARNING: triying to update property of object that does not exist: {} ({})'
                          .format(update_data, e))
                    self.should_request_full_state = True

            elif update_type == "addedChild":
                parent_tree_uuid = update_data[0]
                try:
                    parent_tree_element = self.get_element_with_uuid(parent_tree_uuid)
                    index_in_parent_childs = int(update_data[2])
                    child_soup = next(BeautifulSoup(update_data[3], "lxml").find("body").children)
                    if child_soup.name == 'TRACK'.lower():
                        track = Track(child_soup, self)
                        parent_tree_element.add_track(track, position=index_in_parent_childs)
                        self.add_element_to_uuid_map(track)
                    elif child_soup.name == 'CLIP'.lower():
                        clip = Clip(child_soup, self, parent=parent_tree_element)
                        parent_tree_element.add_clip(clip, position=index_in_parent_childs)
                        self.add_element_to_uuid_map(clip)
                    elif child_soup.name == 'SEQUENCE_EVENT'.lower():
                        sequence_event = SequenceEvent(child_soup, self, parent=parent_tree_element)
                        parent_tree_element.add_sequence_event(sequence_event, position=index_in_parent_childs)
                        self.add_element_to_uuid_map(sequence_event)
                    elif child_soup.name == 'HARDWARE_DEVICE'.lower():
                        hardware_device = HardwareDevice(child_soup, self)
                        parent_tree_element.add_hardware_device(hardware_device, position=index_in_parent_childs)
                        self.add_element_to_uuid_map(hardware_device)
                    else:
                        print('WARNING: trying to add child of a type that can\'t be handled: {}'.format(child_soup))

                except KeyError as e:
                    print('WARNING: triying to add child to parent that does not exist: {} ({})'.format(update_data, e))
                    self.should_request_full_state = True
                    raise e
            
            elif update_type == "removedChild":
                child_to_remove_tree_uuid = update_data[0]
                try:
                    tree_element = self.get_element_with_uuid(child_to_remove_tree_uuid)
                    if isinstance(tree_element, Track):
                        self.state.session.remove_track_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, Clip):
                        tree_element.track.remove_clip_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, SequenceEvent):
                        tree_element.clip.remove_sequence_event_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, HardwareDevice):
                        self.state.remove_hardware_device_with_uuid(child_to_remove_tree_uuid)
                    else:
                        print('WARNING: Trying to remove element of a type that can\'t be handled: {}'
                              .format(tree_element))
                    self.remove_element_from_uuid_map(child_to_remove_tree_uuid)
                except KeyError as e:
                    print('WARNING: triying to remove child with uuid that does not exist: {} ({})'
                          .format(update_data, e))
                    self.should_request_full_state = True

        # Notify app that state was updated
        self.app.on_state_update_received()

    def on_full_state_received(self, full_state_soup):
        self.build_objects_from_full_state(full_state_soup)

        # Notify app that new state has been fully loaded
        self.app.on_backend_state_ready()

    def build_objects_from_full_state(self, full_state_soup):
        self.elements_uuids_map = {}

        # build state root object
        root_element_soup = full_state_soup.findAll("state")[0]
        state = State(root_element_soup, self)  # Add properties from state root object
        self.add_element_to_uuid_map(state)

        # add hardware devices
        hardware_devices_soup = root_element_soup.findAll("hardware_devices")[0]
        for hardware_device_soup in hardware_devices_soup.findAll("hardware_device"):
            hardware_device = HardwareDevice(hardware_device_soup, self)
            self.add_element_to_uuid_map(hardware_device)
            state.add_hardware_device(hardware_device)
        self.state = state

        # add session and all related objects
        session_soup = root_element_soup.findAll("session")[0]
        session = Session(session_soup, self, parent=self.state)
        self.add_element_to_uuid_map(session)
        for track_soup in session_soup.findAll("track"):
            track = Track(track_soup, self, parent=session)
            self.add_element_to_uuid_map(track)
            for count, clip_soup in enumerate(track_soup.findAll("clip")):
                clip = Clip(clip_soup, self, parent=track)
                self.add_element_to_uuid_map(clip)
                for sequence_event_soup in clip_soup.findAll("sequence_event"):
                    sequence_event = SequenceEvent(sequence_event_soup, self, parent=clip)
                    clip.add_sequence_event(sequence_event)
                    self.add_element_to_uuid_map(sequence_event)
                track.add_clip(clip)
            session.add_track(track)
        self.state.session = session

        self.state.send_controller_ready_message_to_backend()
