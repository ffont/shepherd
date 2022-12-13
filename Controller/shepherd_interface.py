from pyshepherd.pyshepherd import ShepherdBackendInterface


class ShepherdInterface(object):

    app = None

    state_transport_check_thread = None
    state_tracks_check_thread = None

    last_received_tracks_raw_state = ""
    parsed_state = {}

    showing_countin_message = False

    def __init__(self, app):
        self.app = app
        self.sbi = ShepherdBackendInterface(app, ws_port=8126, verbose=False, debugger_port=5100)
        self.sbi.send_msg_to_app('/shepherdControllerReady', [])

    @property
    def session(self):
        # Will return None if no session state is loaded
        if self.sbi.state is None:
            return None
        return self.sbi.state.session

    @property
    def state(self):
        # Will return None if no state is loaded
        return self.sbi.state

    def reactivate_modes(self):
        self.app.active_modes_need_reactivate = True

    def receive_shepherd_ready(self):
        self.reactivate_modes()
        self.app.midi_cc_mode.initialize()
        #self.app.notes_midi_in = None # This used to be here in previous controller implementation, not sure if it is needed

    def get_clip_state(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].get_status()
        return 'snE|0.000|0.0'

    def get_clip_length(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].cliplengthinbeats
        return 0.0

    def get_clip_playhead(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].playheadpositioninbeats
        return 0.0

    def get_clip_notes(self, track_num, clip_num):
        if self.session:
            clip = self.session.tracks[track_num].clips[clip_num]
            # type "note" is "1"
            return [event for event in clip.sequence_events if event.is_type_note() and event.renderedstarttimestamp >= 0.0]
        return []

    def get_track_num_clips(self, track_num):
        if self.session:
            return len(self.session.tracks[track_num].clips)
        return 0
    
    def scene_play(self, scene_number):
        self.session.scene_play(scene_number)

    def scene_duplicate(self, scene_number):
        self.session.scene_duplicate(scene_number)
        self.app.add_display_notification("Duplicated scene: {0}".format(scene_number + 1))

    def global_play_stop(self):
        self.session.global_play_stop()

    def global_record(self):
        # Stop all clips that are being recorded
        # If the currently played clip in currently selected track is not recording, start recording it
        if self.session:
            selected_trak_num = self.app.track_selection_mode.selected_track
            for track_num, track in enumerate(self.session.tracks):
                if track_num == selected_trak_num:
                    clip_num = -1
                    for i, clip in enumerate(track.clips):
                        clip_state = clip.get_status()
                        if 'p' in clip_state or 'w' in clip_state:
                            # clip is playing or cued to record, toggle recording on that clip
                            clip_num = i
                            break
                    if clip_num > -1:
                        clip.record_on_off()
                else:
                    for clip_num, clip in enumerate(track.clips):
                        clip_state = clip.get_status()
                        if 'r' in clip_state or 'w' in clip_state:
                            # if clip is recording or cued to record, toggle record so recording/cue are cleared
                            clip.record_on_off()

    def metronome_on_off(self):
        self.session.metronome_on_off()
        self.app.add_display_notification("Metronome: {0}".format('On' if not self.parsed_state.get('metronomeOn', False) else 'Off'))
    
    def get_buttons_state(self):
        if self.session:
            is_playing = self.session.isplaying
            is_recording = False
            for track in self.session.tracks:
                for clip in track.clips:
                    clip_state = clip.get_status()
                    if 'r' in clip_state or 'w' in clip_state or 'W' in clip_state:
                        is_recording = True
                        break
            metronome_on = self.session.metronomeon
            return is_playing, is_recording, metronome_on
        return False, False, False

    def get_bpm(self):
        if self.session:
            return self.session.bpm
        return 0

    def set_bpm(self, bpm):
        self.session.set_bpm(float(bpm))
        self.app.add_display_notification("Tempo: {0} bpm".format(bpm))

    def get_meter(self):
        if self.session:
            return self.session.meter
        return 0

    def set_meter(self, meter):
        self.session.set_meter(int(meter))
        self.app.add_display_notification("Meter: {0} beats".format(meter))

    def get_num_tracks(self):
        if self.session:
            return len(self.session.tracks)
        return 0
        
    def get_track_is_input_monitoring(self, track_num):
        if self.session:
            return self.session.tracks[track_num].inputmonitoring
        return False

    def get_track_device_short_name(self, track_num):
        if self.session:
            return self.session.tracks[track_num].hardwaredevicename
        return "-"

    def get_fixed_length_amount(self):
        if self.session:
            return self.session.fixedlengthrecordingbars
        return 0

    def set_fixed_length_amount(self, fixed_length):
        self.session.set_fix_length_recording_bars(fixed_length)
        if fixed_length > 0:
            self.app.add_display_notification("Fixed length bars: {0} ({1} beats)".format(fixed_length, fixed_length * self.get_meter()))
        else:
            self.app.add_display_notification("No fixed length recording")

    def get_record_automation_enabled(self):
        if self.session:
            return self.session.recordautomationenabled
        return False

    def set_record_automation_enabled(self):
        self.session.set_record_automation_on_off()
        
