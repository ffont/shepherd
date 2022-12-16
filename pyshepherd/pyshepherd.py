from __future__ import annotations

import json
import mido

from bs4 import BeautifulSoup
from typing import Optional, List

from .state_synchronizer import StateSynchronizer
from .state_debugger import start_state_debugger

verbose_level = 0


parameters_types = {
    'allowaftertouchmessages': bool,
    'allowchannelpressuremessages': bool,
    'allowcontrollermessages': bool,
    'allowedmidiinputchannel': int,
    'allownotemessages': bool,
    'allowpitchbendmessages': bool,
    'availablehardwaredevicenames': str,
    'barcount': int,
    'bpm': float,
    'chance': float,
    'cliplengthinbeats': float,
    'controlchangemapping': str,
    'controlchangemessagesarerelative': bool,
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
    'midiinputdevicename': str,
    'midinote': int,
    'midioutputdevicename': str,
    'midivelocity': float,  # . 0.0-1.0
    'name': str,
    'notesmapping': str,
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
            if verbose_level >= 1:
                print('WARNING: unknown parameter {} of received type {}'.format(attr_name, type(value)))
            return value
    except Exception as e:
        raise Exception('Error converting value {} for attribute {}: {}'.format(value, attr_name, str(e)))


class BaseShepherdClass(object):

    _parent = None

    def __init__(self, soup, shepherd_backend_interface, parent=None):
        # Set parent
        self._parent = parent

        # Set state synchronizer (will be used to be able to send messages to backend)
        self.shepherd_backend_interface = shepherd_backend_interface

        # Set initial attributes and methods to set these attributes in the backend
        for attr_name, value in soup.attrs.items():
            setattr(self, attr_name, backend_value_to_python_value(attr_name, value))
    
    def _send_msg_to_app(self, address, values):
        self.shepherd_backend_interface.send_msg_to_app(address, values)

    def render_object_attributes(self, num_spaces_offset=0):
        text = ''
        for attr_name in dir(self):
            if not attr_name.startswith('_') \
                    and not callable(getattr(self, attr_name)) \
                    and not type(getattr(self, attr_name)) == list \
                    and attr_name != 'shepherd_backend_interface' \
                    and attr_name != '_parent' \
                    and attr_name != 'track' \
                    and attr_name != 'clip' \
                    and attr_name != 'state' \
                    and attr_name != 'session':
                text += '{}{}: {}\n'.format(' ' * num_spaces_offset, attr_name, getattr(self, attr_name))
        return text


class State(BaseShepherdClass):
    hardware_devices: List[HardwareDevice] = []
    session = None

    def __init__(self, *args, **kwargs):
        self.hardware_devices = []
        super().__init__(*args, **kwargs)

    def _add_hardware_device(self, hardware_device: HardwareDevice, position=None):
        # Note this method adds a HardwareDevice object in the local State object but does not create a hardware
        # device in the backend
        if position is None:
            self.hardware_devices.append(hardware_device)
        else:
            self.hardware_devices.insert(position, hardware_device)

    def _remove_hardware_device_with_uuid(self, hardware_device_uuid):
        # Note this method removes a HardwareDevice object from the local State object but does not remove a
        # hardware device from the backend
        self.hardware_devices = [hardware_device for hardware_device in self.hardware_devices
                                 if hardware_device.uuid != hardware_device_uuid]

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
        self._send_msg_to_app('/shepherdControllerReady', [])

    def get_input_hardware_device_by_name(self, hardware_device_name):
        for hardware_device in self.hardware_devices:
            if hardware_device.name == hardware_device_name or hardware_device.shortname == hardware_device_name \
                    and hardware_device.type == 0:
                return hardware_device
        return None

    def get_output_hardware_device_by_name(self, hardware_device_name) -> Optional[HardwareDevice]:
        for hardware_device in self.hardware_devices:
            if hardware_device.name == hardware_device_name or hardware_device.shortname == hardware_device_name \
                    and hardware_device.type == 1:
                return hardware_device
        return None
    
    def get_available_output_hardware_device_names(self) -> List[str]:
        return [device.shortname for device in self.hardware_devices if device.is_type_output()]

    def toggle_shepherd_backend_debug_synth(self):
        self._send_msg_to_app('/settings/debugSynthOnOff', [])


class Session(BaseShepherdClass):
    tracks: List[Track] = []

    @property
    def state(self):
        return self._parent

    def __init__(self, *args, **kwargs):
        self.tracks = []
        super().__init__(*args, **kwargs)

    def _add_track(self, track: Track, position=None):
        # Note this method adds a Track object in the local Session object but does not create a track in the backend
        if position is None:
            self.tracks.append(track)
        else:
            self.tracks.insert(position, track)

    def _remove_track_with_uuid(self, track_uuid):
        # Note this method removes a Track object from the local Session object but does not remove a track from
        # the backend
        self.tracks = [track for track in self.tracks if track.uuid != track_uuid]

    def get_track_by_idx(self, track_idx=None) -> Optional[Track]:
        try:
            return self.tracks[track_idx]
        except Exception as e:
            if verbose_level >= 1:
                print('ERROR selecting track: {}'.format(e))
        return None

    def get_clip_by_idx(self, track_idx=None, clip_idx=None) -> Optional[Clip]:
        try:
            return self.tracks[track_idx].clips[clip_idx]
        except Exception as e:
            if verbose_level >= 1:
                print('ERROR selecting track: {}'.format(e))
        return None

    def save(self, save_session_name):
        self._send_msg_to_app('/settings/save', [save_session_name])

    def load(self, load_session_name):
        self._send_msg_to_app('/settings/load', [load_session_name])

    def new(self, num_tracks, num_scenes):
        self._send_msg_to_app('/settings/new', [num_tracks, num_scenes])

    def scene_play(self, scene_number):
        self._send_msg_to_app('/scene/play', [scene_number])

    def scene_duplicate(self, scene_number):
        self._send_msg_to_app('/scene/duplicate', [scene_number])

    def play_stop(self):
        self._send_msg_to_app('/transport/playStop', [])

    def play(self):
        self._send_msg_to_app('/transport/play', [])

    def stop(self):
        self._send_msg_to_app('/transport/stop', [])

    def metronome_on_off(self):
        self._send_msg_to_app('/metronome/onOff', [])

    def set_bpm(self, new_bpm):
        self._send_msg_to_app('/transport/setBpm', [new_bpm])
    
    def set_meter(self, new_meter):
        self._send_msg_to_app('/transport/setMeter', [new_meter])

    def set_fix_length_recording_bars(self, new_fixed_length_recording_bars):
        self._send_msg_to_app('/settings/fixedLength', [new_fixed_length_recording_bars])

    def set_fixed_velocity(self, velocity):
        self._send_msg_to_app('/settings/fixedVelocity', [velocity])

    def set_record_automation_on_off(self):
        self._send_msg_to_app('/settings/toggleRecordAutomation', [])
        

class Track(BaseShepherdClass):
    clips: List[Clip] = []

    @property
    def session(self):
        return self._parent
    
    def __init__(self, *args, **kwargs):
        self.clips = []
        super().__init__(*args, **kwargs)

    def _add_clip(self, clip: Clip, position=None):
        # Note this method adds a Clip object in the local Trck object but does not create a clip in the backend
        if position is None:
            self.clips.append(clip)
        else:
            self.clips.insert(position, clip)

    def _remove_clip_with_uuid(self, clip_uuid):
        # Note this method removes a Clip object from the local Track object but does not remove a clip from the backend
        self.clips = [clip for clip in self.clips if clip.uuid != clip_uuid]

    def get_output_hardware_device(self) -> Optional[HardwareDevice]:
        return self.session.state.get_output_hardware_device_by_name(self.hardwaredevicename)

    def set_input_monitoring(self, enabled):
        self._send_msg_to_app('/track/setInputMonitoring', [self.uuid, 1 if enabled else 0])

    def set_active_ui_notes_monitoring(self):
        self._send_msg_to_app('/track/setActiveUiNotesMonitoringTrack', [self.uuid])

    def set_output_hardware_device(self, device_name):
        self._send_msg_to_app('/track/setOutputHardwareDevice', [self.uuid, device_name])


class Clip(BaseShepherdClass):
    sequence_events: List[SequenceEvent] = []

    @property
    def track(self):
        return self._parent

    def __init__(self, *args, **kwargs):
        self.sequence_events = []
        super().__init__(*args, **kwargs)

    def _add_sequence_event(self, sequence_event: SequenceEvent, position=None):
        # Note this method adds a SequenceEvent object in the local Clip object but does not create a sequence event
        # in the backend
        if position is None:
            self.sequence_events.append(sequence_event)
        else:
            self.sequence_events.insert(position, sequence_event)

    def _remove_sequence_event_with_uuid(self, sequence_event_uuid):
        # Note this method removes a SequenceEvent object from the local Clip object but does not remove a sequence
        # event from the backend
        self.sequence_events = [sequence_event for sequence_event in self.sequence_events
                                if sequence_event.uuid != sequence_event_uuid]

    def get_status(self) -> str:
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
        self._send_msg_to_app('/clip/playStop', [self.track.uuid, self.uuid])

    def play(self):
        self._send_msg_to_app('/clip/play', [self.track.uuid, self.uuid])

    def stop(self):
        self._send_msg_to_app('/clip/stop', [self.track.uuid, self.uuid])
    
    def record_on_off(self):
        self._send_msg_to_app('/clip/recordOnOff', [self.track.uuid, self.uuid])

    def clear(self):
        self._send_msg_to_app('/clip/clear', [self.track.uuid, self.uuid])

    def double(self):
        self._send_msg_to_app('/clip/double', [self.track.uuid, self.uuid])

    def quantize(self, quantization_step):
        self._send_msg_to_app('/clip/quantize', [self.track.uuid, self.uuid, quantization_step])

    def undo(self):
        self._send_msg_to_app('/clip/undo', [self.track.uuid, self.uuid])

    def set_length(self, new_length):
        self._send_msg_to_app('/clip/setLength', [self.track.uuid, self.uuid, new_length])

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
        self._send_msg_to_app("/clip/setSequence", [self.track.uuid, self.uuid, json.dumps(new_sequence)])

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
        self._send_msg_to_app("/clip/editSequence", [self.track.uuid, self.uuid, json.dumps(edit_sequence_data)])

    def remove_sequence_event(self, event_uuid):
        self.edit_sequence({
            'action': 'removeEvent',
            'eventUUID': event_uuid, 
        })

    def add_sequence_note_event(self, midi_note: int, midi_velocity: float, timestamp: float, duration: float,
                                utime: float = 0.0, chance: float = 1.0):
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
        return self._parent

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
        self._send_msg_to_app('/device/sendMidi', [self.name] + msg.bytes())

    def all_notes_off(self):
        self._send_msg_to_app('/device/sendAllNotesOff', [self.name])

    def load_preset(self, bank, preset):
        self._send_msg_to_app('/device/loadDevicePreset', [self.name, bank, preset])

    def get_current_midi_cc_parameter_value(self, midi_cc_num) -> int:
        if self.midiccparametervalueslist != self._midiccparametervalueslist_used_for_splitting:
            self._midiccparametervalueslist_used_for_splitting = self.midiccparametervalueslist
            self._midiccparametervalueslist_splitted = self.midiccparametervalueslist.split(',')
        return int(self._midiccparametervalueslist_splitted[midi_cc_num])

    def set_notes_mapping(self, mapping):
        self._send_msg_to_app('/device/setNotesMapping', [self.name, ",".join([str(item) for item in mapping])])

    def set_control_change_mapping(self, mapping):
        self._send_msg_to_app('/device/setCCMapping', [self.name, ",".join([str(item) for item in mapping])])


class ShepherdBackendInterface(StateSynchronizer):

    app = None
    state = None
    elements_uuids_map = {}

    def __init__(self, *args, **kwargs):
        self.state = None
        self.app = kwargs.get('app', None)
        if 'app' in kwargs: del kwargs['app']
        debugger_port = kwargs.get('debugger_port', None)
        if 'debugger_port' in kwargs: del kwargs['debugger_port']
        super().__init__(*args, **kwargs)
        if debugger_port is not None:
            start_state_debugger(self, debugger_port)

        global verbose_level
        verbose_level = self.verbose_level

    def _add_element_to_uuid_map(self, element):
        self.elements_uuids_map[element.uuid] = element

    def _remove_element_from_uuid_map(self, uuid):
        del self.elements_uuids_map[uuid]

    def get_element_with_uuid(self, uuid):
        return self.elements_uuids_map[uuid]

    def app_has_started(self):
        super().app_has_started()
        if self.app is not None:
            self.app.on_backend_connected()

    def app_connection_lost(self):
        if self.app is not None:
            self.app.on_backend_connection_lost()

    def on_state_update(self, update_type, update_data):
        if self.state is None:
            # If we don't have a session built, request new full state and ignore current state update
            self.should_request_full_state = True
        else:
            app_notification_data = None
            if update_type == "propertyChanged":
                tree_uuid = update_data[0]
                try:
                    tree_element = self.get_element_with_uuid(tree_uuid)
                    property_name = update_data[2].lower()
                    new_value = update_data[3]

                    if hasattr(tree_element, property_name):
                        old_value = getattr(tree_element, property_name, None)
                        setattr(tree_element, property_name, backend_value_to_python_value(property_name, new_value))
                        app_notification_data = {
                            'updateType': update_type,
                            'affectedElement': tree_element,
                            'propertyName': property_name,
                            'oldValue': old_value,
                            'newValue': new_value,
                        }
                    else:
                        raise Exception('Trying to update state value for an attribute that does not exist '
                                        '({} of object {})'.format(property_name, tree_element.uuid))
                    # If we are trying to update an attribute that does not exist (or an element that does not exist),
                    # this will raise an error. This is to be expected as we should never get here trying to update
                    # an attribute/element that does not exist
                except KeyError as e:
                    if self.verbose_level >= 1:
                        print('WARNING: trying to update property of object that does not exist: {} ({})'
                              .format(update_data, e))
                    self.should_request_full_state = True

            elif update_type == "addedChild":
                parent_tree_uuid = update_data[0]
                try:
                    parent_tree_element = self.get_element_with_uuid(parent_tree_uuid)
                    added_tree_element = None
                    index_in_parent_childs = int(update_data[2])
                    child_soup = next(BeautifulSoup(update_data[3], "lxml").find("body").children)
                    if child_soup.name == 'TRACK'.lower():
                        added_tree_element = Track(child_soup, self)
                        parent_tree_element._add_track(added_tree_element, position=index_in_parent_childs)
                        self._add_element_to_uuid_map(added_tree_element)
                    elif child_soup.name == 'CLIP'.lower():
                        added_tree_element = Clip(child_soup, self, parent=parent_tree_element)
                        parent_tree_element._add_clip(added_tree_element, position=index_in_parent_childs)
                        self._add_element_to_uuid_map(added_tree_element)
                    elif child_soup.name == 'SEQUENCE_EVENT'.lower():
                        added_tree_element = SequenceEvent(child_soup, self, parent=parent_tree_element)
                        parent_tree_element._add_sequence_event(added_tree_element, position=index_in_parent_childs)
                        self._add_element_to_uuid_map(added_tree_element)
                    elif child_soup.name == 'HARDWARE_DEVICE'.lower():
                        added_tree_element = HardwareDevice(child_soup, self)
                        parent_tree_element._add_hardware_device(added_tree_element, position=index_in_parent_childs)
                        self._add_element_to_uuid_map(added_tree_element)
                    else:
                        if self.verbose_level >= 1:
                            print('WARNING: trying to add child of a type that can\'t be handled: {}'
                                  .format(child_soup))
                    app_notification_data = {
                        'updateType': update_type,
                        'parentElement': parent_tree_element,
                        'addedElement': added_tree_element
                    }
                except KeyError as e:
                    if self.verbose_level >= 1:
                        print('WARNING: trying to add child to parent that does not exist: {} ({})'
                              .format(update_data, e))
                    self.should_request_full_state = True
                    raise e
            
            elif update_type == "removedChild":
                child_to_remove_tree_uuid = update_data[0]
                try:
                    tree_element = self.get_element_with_uuid(child_to_remove_tree_uuid)
                    parent_tree_element = None
                    removed_element_type = None
                    if isinstance(tree_element, Track):
                        parent_tree_element = self.state.session
                        removed_element_type = Track
                        self.state.session._remove_track_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, Clip):
                        parent_tree_element = tree_element.track
                        removed_element_type = Clip
                        tree_element.track._remove_clip_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, SequenceEvent):
                        parent_tree_element = tree_element.clip
                        removed_element_type = SequenceEvent
                        tree_element.clip._remove_sequence_event_with_uuid(child_to_remove_tree_uuid)
                    elif isinstance(tree_element, HardwareDevice):
                        parent_tree_element = self.state
                        removed_element_type = HardwareDevice
                        self.state._remove_hardware_device_with_uuid(child_to_remove_tree_uuid)
                    else:
                        if self.verbose_level >= 1:
                            print('WARNING: Trying to remove element of a type that can\'t be handled: {}'
                                  .format(tree_element))
                    self._remove_element_from_uuid_map(child_to_remove_tree_uuid)
                    app_notification_data = {
                        'updateType': update_type,
                        'parentElement': parent_tree_element,
                        'removedElementType': removed_element_type,
                        'removedElementUUID': child_to_remove_tree_uuid,
                    }
                except KeyError as e:
                    if self.verbose_level >= 1:
                        print('WARNING: triying to remove child with uuid that does not exist: {} ({})'
                              .format(update_data, e))
                    self.should_request_full_state = True

            # Notify app that state was updated
            if self.app is not None:
                self.app.on_state_update_received(app_notification_data)

    def on_full_state_received(self, full_state_soup):
        self.build_objects_from_full_state(full_state_soup)

        # Notify app that new state has been fully loaded
        if self.app is not None:
            self.app.on_backend_state_ready()

    def build_objects_from_full_state(self, full_state_soup):
        self.elements_uuids_map = {}

        # build state root object
        root_element_soup = full_state_soup.findAll("state")[0]
        state = State(root_element_soup, self)  # Add properties from state root object
        self._add_element_to_uuid_map(state)

        # add hardware devices
        hardware_devices_soup = root_element_soup.findAll("hardware_devices")[0]
        for hardware_device_soup in hardware_devices_soup.findAll("hardware_device"):
            hardware_device = HardwareDevice(hardware_device_soup, self)
            self._add_element_to_uuid_map(hardware_device)
            state._add_hardware_device(hardware_device)
        self.state = state

        # add session and all related objects
        session_soup = root_element_soup.findAll("session")[0]
        session = Session(session_soup, self, parent=self.state)
        self._add_element_to_uuid_map(session)
        for tn, track_soup in enumerate(session_soup.findAll("track")):
            track = Track(track_soup, self, parent=session)
            self._add_element_to_uuid_map(track)
            for cn, clip_soup in enumerate(track_soup.findAll("clip")):
                clip = Clip(clip_soup, self, parent=track)
                self._add_element_to_uuid_map(clip)
                for sen, sequence_event_soup in enumerate(clip_soup.findAll("sequence_event")):
                    sequence_event = SequenceEvent(sequence_event_soup, self, parent=clip)
                    clip._add_sequence_event(sequence_event)
                    self._add_element_to_uuid_map(sequence_event)
                track._add_clip(clip)
            session._add_track(track)
        self.state.session = session

        self.state.send_controller_ready_message_to_backend()


class ShepherdBackendControllerApp(object):

    shepherd_interface: ShepherdBackendInterface

    def __init__(self, ws_port=8126, verbose_level=1, debugger_port=None):
        self.shepherd_interface = ShepherdBackendInterface(app=self,
                                                           ws_port=ws_port,
                                                           verbose_level=verbose_level,
                                                           debugger_port=debugger_port)

    @property
    def state(self) -> Optional[State]:
        try:
            return self.shepherd_interface.state
        except AttributeError as e:
            pass
        return None

    @property
    def session(self) -> Optional[Session]:
        try:
            return self.shepherd_interface.state.session
        except AttributeError as e:
            pass
        return None

    def on_backend_connected(self):
        pass

    def on_backend_connection_lost(self):
        pass

    def on_backend_state_ready(self):
        pass

    def on_state_update_received(self, update_data):
        pass
