from oscpy.client import OSCClient
from oscpy.server import OSCThreadServer
import threading
import time
import math

osc_send_host = "127.0.0.1"
osc_send_port = 9003
osc_receive_port = 9004

tracks_state_fps = 4.0
transport_state_fps = 10.0

class ShepherdInterface(object):

    app = None

    osc_sender = None
    osc_server = None

    state_transport_check_thread = None
    state_tracks_check_thread = None

    last_received_tracks_raw_state = ""
    parsed_state = {}

    should_sync_state_with_backend = False

    showing_countin_message = False

    def __init__(self, app):
        self.app = app

        self.osc_sender = OSCClient(osc_send_host, osc_send_port, encoding='utf8')

        self.osc_server = OSCThreadServer()
        sock = self.osc_server.listen(address='0.0.0.0', port=osc_receive_port, default=True)
        self.osc_server.bind(b'/shepherdReady', self.receive_shepherd_ready)
        self.osc_server.bind(b'/stateFromShepherd', self.receive_state_from_shepherd)
        self.osc_server.bind(b'/midiCCParameterValuesForDevice', self.receive_midi_cc_values_for_device)
        
        # Send first message notifying backend that controller is ready and start threads that 
        # request periodic state updates
        self.osc_sender.send_message('/shepherdControllerReady', [])
        self.run_get_state_transport_thread()
        self.run_get_state_tracks_thread()

    def run_get_state_transport_thread(self):
        self.state_transport_check_thread = threading.Thread(target=self.check_transport_state)
        self.state_transport_check_thread.start()

    def run_get_state_tracks_thread(self):
        self.state_tracks_check_thread = threading.Thread(target=self.check_tracks_state)
        self.state_tracks_check_thread.start()

    def check_transport_state(self):
        while True:
            time.sleep(1.0/transport_state_fps)
            self.osc_sender.send_message('/state/transport', [])

    def check_tracks_state(self):
        while True:
            time.sleep(1.0/tracks_state_fps)
            self.osc_sender.send_message('/state/tracks', [])

    def request_midi_cc_values_for_device(self, device_name, ccs):
        self.osc_sender.send_message('/device/getMidiCCParameterValues', [device_name] + ccs)

    def sync_state_to_shepherd(self):
        # re-activate all modes to make sure we initialize things in the backend if needed
        print('Synching with Shepherd backend state')
        for mode in self.app.active_modes:
            mode.activate()
        self.app.midi_cc_mode.initialize()
        self.app.init_notes_midi_in()
        self.should_sync_state_with_backend = False

    def receive_shepherd_ready(self):
        self.should_sync_state_with_backend = True

    def receive_midi_cc_values_for_device(self, *values):
        device_name = values[0].decode("utf-8")        
        if 'devices' not in self.parsed_state:
            self.parsed_state['devices'] = {}
        if device_name not in self.parsed_state['devices']:
            self.parsed_state['devices'][device_name] = {}
        if 'midi_cc' not in self.parsed_state['devices'][device_name]:
            self.parsed_state['devices'][device_name]['midi_cc'] = [64 for i in range(0, 128)]
        if len(values) > 1:
            for i in range(1, len(values) - 1):
                self.parsed_state['devices'][device_name]['midi_cc'][int(values[i])] = int(values[i + 1])
        
    def receive_state_from_shepherd(self, values):
        if not self.parsed_state:
            # If this is the first time receiving state, schedule full sync
            self.should_sync_state_with_backend = True

        state = values.decode("utf-8")
        if state.startswith("transport"):
            parts = state.split(',')
            old_is_playing = self.parsed_state.get('isPlaying', False)
            old_is_recording = self.parsed_state.get('isRecording', False)
            old_metronome_on = self.parsed_state.get('metronomeOn', False)
            old_record_automation_on = self.parsed_state.get('recordAutomaionOn', False)
            self.parsed_state['isPlaying'] = parts[1] == "p"
            if 'tracks' in self.parsed_state:
                is_recording = False
                for track_state in self.parsed_state['tracks']:
                    track_clips = track_state['clips']
                    for clip in track_clips:
                        if 'r' in clip or 'w' in clip or 'W' in clip:
                            is_recording = True
                            break
                self.parsed_state['isRecording'] = is_recording
            else:
                self.parsed_state['isRecording'] = False
            self.parsed_state['bpm'] = float(parts[2])
            self.parsed_state['playhead'] = float(parts[3])
            if self.parsed_state['playhead'] < 0.0:
                self.showing_countin_message = True
                self.app.add_display_notification("Will start recording in: {0:.0f}".format(math.ceil(-1 * self.parsed_state['playhead'])))
            else:
                if self.showing_countin_message:
                    self.app.clear_display_notification()
                    self.showing_countin_message = False

            self.parsed_state['metronomeOn'] = parts[4] == "p"

            # Initialize clip playheads matrix to 0s
            clipPlayheads = []
            for track_num in range(0, self.get_num_tracks()):
                track_clip_playheads = []
                for clip_num in range(0, self.get_track_num_clips(track_num)):
                    track_clip_playheads.append(0.0)
                clipPlayheads.append(track_clip_playheads)
            # Fill matrix with info from state
            if clipPlayheads:
                clip_playheads_state_info_parts = parts[5].split(':')
                if len(clip_playheads_state_info_parts) > 1:
                    for i in range(0, len(clip_playheads_state_info_parts), 3):
                        track_num = int(clip_playheads_state_info_parts[i])
                        clip_num = int(clip_playheads_state_info_parts[i + 1])
                        playhead = float(clip_playheads_state_info_parts[i + 2])
                        clipPlayheads[track_num][clip_num] = playhead
                self.parsed_state['clipPlayheads'] = clipPlayheads
            
            self.parsed_state['fixedLengthRecordingAmount'] = int(parts[6])
            self.parsed_state['meter'] = int(parts[7])
            self.parsed_state['recordAutomaionOn'] = parts[8] == "1"

            if old_is_playing != self.parsed_state['isPlaying'] or \
                old_is_recording != self.parsed_state['isRecording'] or \
                    old_metronome_on != self.parsed_state['metronomeOn'] or \
                        old_record_automation_on != self.parsed_state['recordAutomaionOn']:
                self.app.buttons_need_update = True

        elif state.startswith("tracks"):
            old_num_tracks = self.parsed_state.get('numTracks', 0)
            if state != self.last_received_tracks_raw_state:
                parts = state.split(',')
                self.parsed_state['numTracks'] = int(parts[1])
                tracks_state = []
                current_track_clips_state = []
                in_track = False
                for part in parts:
                    if part == "t":
                        in_track = True
                        if current_track_clips_state:
                            tracks_state.append({
                                    'numClips': int(current_track_clips_state[0]),
                                    'inputMonitoring': current_track_clips_state[1] == "1",
                                    'deviceShortName': current_track_clips_state[2],
                                    'clips': current_track_clips_state[3:]
                                }) 
                        current_track_clips_state = []
                    else:
                        if in_track:
                            current_track_clips_state.append(part)
                if current_track_clips_state:
                    tracks_state.append({
                        'numClips': int(current_track_clips_state[0]),
                        'inputMonitoring': current_track_clips_state[1] == "1",
                        'deviceShortName': current_track_clips_state[2],
                        'clips': current_track_clips_state[3:]
                    })  # Add last one

                self.parsed_state['tracks'] = tracks_state
                self.app.pads_need_update = True
                self.last_received_tracks_raw_state = state

            if old_num_tracks != self.parsed_state.get('numTracks', 0):
                try:
                    self.app.midi_cc_mode.initialize()
                except AttributeError:
                    # Mode has not yet been created in app...
                    pass

        if 'tracks' in self.parsed_state and 'bpm' in self.parsed_state and self.should_sync_state_with_backend:
            # Once full state has been received from backend, sync back to it
            # We need to first receive full state because some of the things to set up (like current tracks with direct monitoring)
            # depend on an intepretation of backend state plus the frontend state
            self.sync_state_to_shepherd()

    def track_select(self, track_num):
        num_tracks = self.parsed_state.get('numTracks', -1)
        if num_tracks > -1:
            for i in range(0, num_tracks):
                self.track_set_input_monitoring(i, i == track_num)

    def track_set_input_monitoring(self, track_num, enabled):
        self.osc_sender.send_message('/track/setInputMonitoring', [track_num, 1 if enabled else 0])

    def track_set_active_ui_notes_monitoring(self, track_num):
        self.osc_sender.send_message('/track/setActiveUiNotesMonitoringTrack', [track_num])

    def device_send_all_notes_off(self, device_name):
        self.osc_sender.send_message('/device/sendAllNotesOff', [device_name])

    def device_load_preset(self, device_name, bank, preset):
        self.osc_sender.send_message('/device/loadDevicePreset', [device_name, bank, preset])

    def device_send_midi(self, device_name, msg):
        self.osc_sender.send_message('/device/sendMidi', [device_name] + msg.bytes())

    def device_get_midi_cc_parameter_value(self, device_name, midi_cc_parameter):
        if 'devices' in self.parsed_state:
            if device_name in self.parsed_state['devices']:
                if 'midi_cc' in self.parsed_state['devices'][device_name]:
                    return self.parsed_state['devices'][device_name]['midi_cc'][midi_cc_parameter]
        return 0
        
    def clip_play_stop(self, track_num, clip_num):
        self.osc_sender.send_message('/clip/playStop', [track_num, clip_num])

    def clip_record_on_off(self, track_num, clip_num):
        self.osc_sender.send_message('/clip/recordOnOff', [track_num, clip_num])

    def clip_clear(self, track_num, clip_num):
        if not self.clip_is_empty(track_num, clip_num):
            self.osc_sender.send_message('/clip/clear', [track_num, clip_num])
            self.app.add_display_notification("Cleared clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_double(self, track_num, clip_num):
        if not self.clip_is_empty(track_num, clip_num):
            self.osc_sender.send_message('/clip/double', [track_num, clip_num])
            self.app.add_display_notification("Doubled clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_quantize(self, track_num, clip_num, quantization_step):
        if not self.clip_is_empty(track_num, clip_num):
            self.osc_sender.send_message('/clip/quantize', [track_num, clip_num, quantization_step])
            quantization_step_labels = {
                0.25: '16th note',
                0.5: '8th note',
                1.0: '4th note',
                0.0: 'no quantization'
            }
            self.app.add_display_notification("Quantized clip to {0}: {1}-{2}".format(quantization_step_labels.get(quantization_step,
                                                                                                           quantization_step), track_num + 1, clip_num + 1))
    def clip_undo(self, track_num, clip_num):
        if not self.clip_is_empty(track_num, clip_num):
            self.osc_sender.send_message('/clip/undo', [track_num, clip_num])
            self.app.add_display_notification("Undo clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_set_length(self, track_num, clip_num, new_length):
        if not self.clip_is_empty(track_num, clip_num):
            self.osc_sender.send_message('/clip/setLength', [track_num, clip_num, new_length])

    def clip_is_empty(self, track_num, clip_num):
        if 'tracks' in self.parsed_state:
            try:
                return 'E' in self.parsed_state['tracks'][track_num]['clips'][clip_num]
            except IndexError:
                return True
        else:
            return True

    def get_clip_state(self, track_num, clip_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['clips'][clip_num]
            except IndexError:
                return 'snE|0.000|0.0'
        else:
            return 'snE|0.000|0.0'

    def get_clip_length(self, track_num, clip_num):
        if 'tracks' in self.parsed_state:
            try:
                return float(self.parsed_state['tracks'][track_num]['clips'][clip_num].split('|')[1])
            except IndexError:
                return 0.0
        else:
            return 0.0

    def get_clip_quantization_step(self, track_num, clip_num):
        if 'tracks' in self.parsed_state:
            try:
                return float(self.parsed_state['tracks'][track_num]['clips'][clip_num].split('|')[2])
            except IndexError:
                return 0.0
        else:
            return 0.0

    def get_clip_playhead(self, track_num, clip_num):
        if 'clipPlayheads' in self.parsed_state:
            return self.parsed_state['clipPlayheads'][track_num][clip_num]
        else:
            return 0.0

    def get_track_num_clips(self, track_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['numClips']
            except IndexError:
                return 0
        else:
            return 0

    def scene_play(self, scene_number):
        self.osc_sender.send_message('/scene/play', [scene_number])

    def scene_duplicate(self, scene_number):
        self.osc_sender.send_message('/scene/duplicate', [scene_number])
        self.app.add_display_notification("Duplicated scene: {0}".format(scene_number + 1))

    def global_play_stop(self):
        self.osc_sender.send_message('/transport/playStop', [])

    def global_record(self):
        # Stop all clips that are being recorded
        # If the currently played clip in currently selected track is not recording, start recording it
        selected_trak_num = self.app.track_selection_mode.selected_track
        for track_num, track in enumerate(self.parsed_state['tracks']):
            if track_num == selected_trak_num:
                clip_num = -1
                for i, clip_state in enumerate(track['clips']):
                    if 'p' in clip_state or 'w' in clip_state:
                        # clip is playing or cued to record, toggle recording on that clip
                        clip_num = i
                        break
                if clip_num > -1:
                    self.osc_sender.send_message('/clip/recordOnOff', [track_num, clip_num])
            else:
                for clip_num, clip_state in enumerate(track['clips']):
                    if 'r' in clip_state or 'w' in clip_state:
                        # if clip is recording or cued to record, toggle record so recording/cue are cleared
                        self.osc_sender.send_message('/clip/recordOnOff', [track_num, clip_num])

    def metronome_on_off(self):
        self.osc_sender.send_message('/metronome/onOff', [])
        self.app.add_display_notification("Metronome: {0}".format('On' if not self.parsed_state.get('metronomeOn', False) else 'Off'))

    def set_push_pads_mapping(self, new_mapping=[]):
        if new_mapping:
            self.osc_sender.send_message('/settings/pushNotesMapping', new_mapping)

    def set_push_encoders_mapping(self, device_name, new_mapping=[]):
        if device_name == "":
            device_name = "-"
        if new_mapping:
            self.osc_sender.send_message('/settings/pushEncodersMapping', [device_name] + new_mapping)

    def set_fixed_velocity(self, velocity):
        self.osc_sender.send_message('/settings/fixedVelocity', [velocity])
        
    def get_buttons_state(self):
        is_playing = self.parsed_state.get('isPlaying', False)
        is_recording = self.parsed_state.get('isRecording', False)
        metronome_on = self.parsed_state.get('metronomeOn', False)
        return is_playing, is_recording, metronome_on

    def get_bpm(self):
        return self.parsed_state.get('bpm', 120)

    def set_bpm(self, bpm):
        self.osc_sender.send_message('/transport/setBpm', [float(bpm)])
        self.app.add_display_notification("Tempo: {0} bpm".format(bpm))

    def get_meter(self):
        return self.parsed_state.get('meter', 4)

    def set_meter(self, meter):
        self.osc_sender.send_message('/transport/setMeter', [int(meter)])
        self.app.add_display_notification("Meter: {0} beats".format(meter))

    def get_num_tracks(self):
        # return self.parsed_state.get('numTracks', 0)
        return len(self.parsed_state.get('tracks', []))

    def get_track_is_input_monitoring(self, track_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['inputMonitoring']
            except IndexError:
                return False
        else:
            return False

    def get_track_device_short_name(self, track_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['deviceShortName']
            except IndexError:
                return ""
        else:
            return ""

    def get_fixed_length_amount(self):
        return self.parsed_state.get('fixedLengthRecordingAmount', 0)

    def set_fixed_length_amount(self, fixed_length):
        self.osc_sender.send_message('/settings/fixedLength', [fixed_length])
        if fixed_length > 0:
            self.app.add_display_notification("Fixed length bars: {0} ({1} beats)".format(fixed_length, fixed_length * self.get_meter()))
        else:
            self.app.add_display_notification("No fixed length recording")

    def get_record_automation_enabled(self):
        return self.parsed_state.get('recordAutomaionOn', False)

    def set_record_automation_enabled(self):
        self.osc_sender.send_message('/settings/toggleRecordAutomation', [])


    
