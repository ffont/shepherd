import threading
import time
import math

from state_synchronizer import ShepherdStateSynchronizer

tracks_state_fps = 4.0
transport_state_fps = 10.0


class ShepherdInterface(object):

    app = None

    state_transport_check_thread = None
    state_tracks_check_thread = None

    last_received_tracks_raw_state = ""
    parsed_state = {}

    showing_countin_message = False

    def __init__(self, app):
        self.app = app

        self.sss = ShepherdStateSynchronizer(app, verbose=False)
        
        # Send first message notifying backend that controller is ready and start threads that 
        # request periodic state updates
        self.sss.send_msg_to_app('/shepherdControllerReady', [])

    @property
    def session(self):
        # Will return None if no session state is loaded
        return self.sss.session

    def reactivate_modes(self):
        self.app.active_modes_need_reactivate = True

    def receive_shepherd_ready(self):
        self.reactivate_modes()
        self.app.midi_cc_mode.initialize()
        #self.app.notes_midi_in = None # This used to be here in previous controller implementation, not sure if it is needed

    def receive_midi_cc_values_for_device(self, *values):
        try:
            device_name = values[0].decode("utf-8")        
        except AttributeError:
            device_name = values[0]
        if 'devices' not in self.parsed_state:
            self.parsed_state['devices'] = {}
        if device_name not in self.parsed_state['devices']:
            self.parsed_state['devices'][device_name] = {}
        if 'midi_cc' not in self.parsed_state['devices'][device_name]:
            self.parsed_state['devices'][device_name]['midi_cc'] = [64 for i in range(0, 128)]
        if len(values) > 1:
            for i in range(1, len(values) - 1):
                self.parsed_state['devices'][device_name]['midi_cc'][int(values[i])] = int(values[i + 1])
        
    def track_select(self, track_num):
        if not self.session: return
        num_tracks = self.get_num_tracks()
        if num_tracks > -1:
            for i in range(0, num_tracks):
                self.session.tracks[i].set_input_monitoring(i==track_num)

    def track_set_input_monitoring(self, track_num, enabled):
        if not self.session: return
        self.session.tracks[track_num].set_input_monitoring(enabled)

    def track_set_active_ui_notes_monitoring(self, track_num):
        if not self.session: return
        self.session.tracks[track_num].set_active_ui_notes_monitoring()

    def device_send_all_notes_off(self, device_name):
        self.sss.send_msg_to_app('/device/sendAllNotesOff', [device_name])

    def device_load_preset(self, device_name, bank, preset):
        self.sss.send_msg_to_app('/device/loadDevicePreset', [device_name, bank, preset])

    def device_send_midi(self, device_name, msg):
        self.sss.send_msg_to_app('/device/sendMidi', [device_name] + msg.bytes())

    def device_get_midi_cc_parameter_value(self, device_name, midi_cc_parameter):
        if 'devices' in self.parsed_state:
            if device_name in self.parsed_state['devices']:
                if 'midi_cc' in self.parsed_state['devices'][device_name]:
                    return self.parsed_state['devices'][device_name]['midi_cc'][midi_cc_parameter]
        return 0
        
    def clip_play_stop(self, track_num, clip_num):
        if not self.session: return
        self.session.tracks[track_num].clips[clip_num].play_stop()

    def clip_record_on_off(self, track_num, clip_num):
        if not self.session: return
        self.session.tracks[track_num].clips[clip_num].record_on_off()

    def clip_clear(self, track_num, clip_num):
        if not self.session: return
        clip = self.session.tracks[track_num].clips[clip_num]
        if not clip.is_empty():
            clip.clear()
            self.app.add_display_notification("Cleared clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_double(self, track_num, clip_num):
        if not self.session: return
        clip = self.session.tracks[track_num].clips[clip_num]
        if not clip.is_empty():
            clip.double()
            self.app.add_display_notification("Doubled clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_quantize(self, track_num, clip_num, quantization_step):
        if not self.session: return
        clip = self.session.tracks[track_num].clips[clip_num]
        if not clip.is_empty():
            clip.quantize(quantization_step)
            quantization_step_labels = {
                0.25: '16th note',
                0.5: '8th note',
                1.0: '4th note',
                0.0: 'no quantization'
            }
            self.app.add_display_notification("Quantized clip to {0}: {1}-{2}".format(quantization_step_labels.get(quantization_step,
                                                                                                           quantization_step), track_num + 1, clip_num + 1))
    def clip_undo(self, track_num, clip_num):
        if not self.session: return
        clip = self.session.tracks[track_num].clips[clip_num]
        if not clip.is_empty():
            clip.undo()
            self.app.add_display_notification("Undo clip: {0}-{1}".format(track_num + 1, clip_num + 1))

    def clip_set_length(self, track_num, clip_num, new_length):
        if not self.session: return
        clip = self.session.tracks[track_num].clips[clip_num]
        if not clip.is_empty():
            clip.set_length(new_length)

    def clip_is_empty(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].is_empty()
        return True

    def get_clip_state(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].get_status()
        return 'snE|0.000|0.0'

    def get_clip_length(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].cliplengthinbeats
        return 0.0

    def get_clip_quantization_step(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].currentquantizationstep
        return 0.0

    def get_clip_playhead(self, track_num, clip_num):
        if self.session:
            return self.session.tracks[track_num].clips[clip_num].playheadpositioninbeats
        return 0.0

    def get_clip_notes(self, track_num, clip_num):
        if self.session:
            clip = self.session.tracks[track_num].clips[clip_num]
            # type "note" is "1"
            return [event for event in clip.sequence_events if event.type == 1 and event.renderedstarttimestamp >= 0.0]
        return []

    def get_track_num_clips(self, track_num):
        if self.session:
            return len(self.session.tracks[track_num].clips)
        return 0

    def is_track_enabled(self, track_num):
        if self.session:
            return self.session.tracks[track_num].enabled
        return False
    
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

    def set_push_pads_mapping(self, new_mapping=[]):
        if new_mapping:
            self.sss.send_msg_to_app('/settings/pushNotesMapping', new_mapping)

    def set_push_encoders_mapping(self, device_name, new_mapping=[]):
        if device_name == "":
            device_name = "-"
        if new_mapping:
            self.sss.send_msg_to_app('/settings/pushEncodersMapping', [device_name] + new_mapping)

    def set_fixed_velocity(self, velocity):
        self.sss.send_msg_to_app('/settings/fixedVelocity', [velocity])
        
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
            self.session.meter
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
        
