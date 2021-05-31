from oscpy.client import OSCClient
from oscpy.server import OSCThreadServer
import threading
import asyncio
import time

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

    def __init__(self, app):
        self.app = app

        self.osc_sender = OSCClient(osc_send_host, osc_send_port, encoding='utf8')

        self.osc_server = OSCThreadServer()
        sock = self.osc_server.listen(address='0.0.0.0', port=osc_receive_port, default=True)
        self.osc_server.bind(b'/shepherdReady', self.receive_shepherd_ready)
        self.osc_server.bind(b'/stateFromShepherd', self.receive_state_from_shepherd)
        
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

    def sync_state_to_shepherd(self):
        # re-activate all modes to make sure we initialize things in the backend if needed
        for mode in self.app.active_modes:
            mode.activate()
        self.should_sync_state_with_backend = False

    def receive_shepherd_ready(self):
        self.should_sync_state_with_backend = True
        

    def receive_state_from_shepherd(self, values):
        state = values.decode("utf-8")
        if state.startswith("transport"):
            parts = state.split(',')
            old_is_playing = self.parsed_state.get('isPlaying', False)
            old_is_recording = self.parsed_state.get('isRecording', False)
            old_metronome_on = self.parsed_state.get('metronomeOn', False)
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
            self.parsed_state['playhead'] = parts[3]
            self.parsed_state['metronomeOn'] = parts[4] == "p"

            if old_is_playing != self.parsed_state['isPlaying'] or \
                old_is_recording != self.parsed_state['isRecording'] or \
                    old_metronome_on != self.parsed_state['metronomeOn']:
                self.app.buttons_need_update = True

        elif state.startswith("tracks"):
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
                                    'clips': current_track_clips_state[2:]
                                }) 
                        current_track_clips_state = []
                    else:
                        if in_track:
                            current_track_clips_state.append(part)
                if current_track_clips_state:
                    tracks_state.append({
                        'numClips': int(current_track_clips_state[0]),
                        'inputMonitoring': current_track_clips_state[1] == "1",
                        'clips': current_track_clips_state[2:]
                    })  # Add last one

                self.parsed_state['tracks'] = tracks_state
                self.app.pads_need_update = True
                self.last_received_tracks_raw_state = state

        if 'tracks' in self.parsed_state and 'bpm' in self.parsed_state and self.should_sync_state_with_backend:
            # Once full state has been received from backend, sync back to it
            # We need to first receive full state because some of the things to set up (like current tracks with direct monitoring)
            # depend on an intepretation of backend state plus the frontend state
            self.sync_state_to_shepherd()

    def track_select(self, track_number):
        num_tracks = self.parsed_state.get('numTracks', -1)
        if num_tracks > -1:
            for i in range(0, num_tracks):
                self.track_set_input_monitoring(i, i == track_number)

    def track_set_input_monitoring(self, track_number, enabled):
        self.osc_sender.send_message('/track/setInputMonitoring', [track_number, 1 if enabled else 0])

    def clip_play_stop(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/playStop', [track_number, clip_number])

    def clip_record_on_off(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/recordOnOff', [track_number, clip_number])

    def clip_clear(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/clear', [track_number, clip_number])

    def clip_double(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/double', [track_number, clip_number])

    def clip_quantize(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/quantize', [track_number, clip_number])

    def clip_undo(self, track_number, clip_number):
        self.osc_sender.send_message('/clip/undo', [track_number, clip_number])

    def get_clip_state(self, track_num, clip_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['clips'][clip_num]
            except IndexError:
                return "snE"
        else:
            return 'snE'

    def get_track_is_input_monitoring(self, track_num):
        if 'tracks' in self.parsed_state:
            try:
                return self.parsed_state['tracks'][track_num]['inputMonitoring']
            except IndexError:
                return False
        else:
            return False

    def scene_play(self, scene_number):
        self.osc_sender.send_message('/scene/play', [scene_number])

    def scene_duplicate(self, scene_number):
        self.osc_sender.send_message('/scene/duplicate', [scene_number])

    def global_play_stop(self):
        self.osc_sender.send_message('/transport/playStop', [])

    def global_record(self):
        # If currently selected track has a playing clip, toggle recording on that clip
        if 'tracks' in self.parsed_state:
            track_num = self.app.track_selection_mode.selected_track
            track = self.parsed_state['tracks'][track_num]
            clip_num = -1
            for i, clip_state in enumerate(track['clips']):
                if 'p' in clip_state:
                    # clip is playing, toggle recording on that clip
                    clip_num = i
                    break
            if clip_num >  -1:
                self.osc_sender.send_message('/clip/recordOnOff', [track_num, clip_num])

    def metronome_on_off(self):
        self.osc_sender.send_message('/metronome/onOff', [])

    def set_push_pads_mapping(self, new_mapping=[]):
        if new_mapping:
            self.osc_sender.send_message('/settings/pushNotesMapping', new_mapping)

    def set_push_encoders_mapping(self, new_mapping=[]):
        if new_mapping:
            self.osc_sender.send_message('/settings/pushEncodersMapping', new_mapping)

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
