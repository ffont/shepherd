import definitions
import push2_python
import time
import random
import math

from display_utils import show_title, show_value, draw_clip


class ClipEditgMode(definitions.ShepherdControllerMode):

    xor_group = 'pads'

    selected_clip_uuid = None
    available_clips = []
    
    buttons_used = [
        push2_python.constants.BUTTON_UPPER_ROW_1,
        push2_python.constants.BUTTON_UPPER_ROW_2,
        push2_python.constants.BUTTON_UPPER_ROW_3,
        push2_python.constants.BUTTON_UPPER_ROW_4,
        push2_python.constants.BUTTON_UPPER_ROW_5,
        push2_python.constants.BUTTON_UPPER_ROW_6,
        push2_python.constants.BUTTON_UPPER_ROW_7,
        push2_python.constants.BUTTON_UPPER_ROW_8,
        push2_python.constants.BUTTON_UP,
        push2_python.constants.BUTTON_DOWN,
        push2_python.constants.BUTTON_LEFT,
        push2_python.constants.BUTTON_RIGHT,
        push2_python.constants.BUTTON_DOUBLE_LOOP,
        push2_python.constants.BUTTON_QUANTIZE,
    ]

    pads_min_note_offset = 64
    pads_pad_beats_offset = 0.0 # Offset for notes to be shown
    pads_pad_beat_scale = 0.5 # Default, 1 pad is one half of a beat, there fore 8 pads are 1 bar (assuming 4/4)

    pads_pad_beat_scales = [0.125 + 0.125 * i for i in range(0, 32)]

    last_beats_to_pad = -1

    '''
    Slot 1 = select clip
    Slot 2 = clip length
    Slot 3 = quantization
    Slot 4 = view scale
    Slots 5-8 = clip preview 
    '''

    @property
    def clip(self):
        if self.selected_clip_uuid is not None:
            return self.app.shepherd_interface.sss.get_element_with_uuid(self.selected_clip_uuid)
        else:
            return None

    @property
    def start_displayed_time(self):
        return self.pads_pad_beats_offset

    @property
    def end_displayed_time(self):
        return self.pads_pad_beats_offset + self.pads_pad_beat_scale * 8

    def pad_ij_to_note_beat(self, pad_ij):
        note = self.pads_min_note_offset + (7 - pad_ij[0])
        beat = pad_ij[1] * self.pads_pad_beat_scale + self.pads_pad_beats_offset
        return note, beat

    def notes_in_pad(self, pad_ij):
        midi_note, start_time = self.pad_ij_to_note_beat(pad_ij)
        end_time = start_time + self.pads_pad_beat_scale
        notes = [event for event in self.clip.sequence_events if event.type == 1 and 
                                                                 start_time <= event.renderedstarttimestamp <= end_time and
                                                                 event.midinote == midi_note]
        return notes

    def beats_to_pad(self, beats):
        return int(math.floor(8 * (beats - self.start_displayed_time)/(self.end_displayed_time - self.start_displayed_time)))

    def notes_to_pads(self):
        notes = [event for event in self.clip.sequence_events if event.type == 1 and 
                                                                 (event.renderedstarttimestamp < self.end_displayed_time or event.renderedendtimestamp > self.start_displayed_time) and
                                                                 self.pads_min_note_offset <= event.midinote < self.pads_min_note_offset + 8]
        notes_to_display = []
        for event in notes:
            duration = event.renderedendtimestamp - event.renderedstarttimestamp
            if duration < 0.0:
                duration = duration + self.clip.cliplengthinbeats
            notes_to_display.append({
                'pad_start_ij': (7 - (event.midinote - self.pads_min_note_offset), 
                                int(math.floor((event.renderedstarttimestamp - self.pads_pad_beats_offset)/(self.pads_pad_beat_scale)))),
                'duration_n_pads': int(math.ceil((duration) / self.pads_pad_beat_scale)),
            })
        track_color = self.app.track_selection_mode.get_track_color(self.clip.track.order)
        color_matrix = []
        animation_matrix = []
        for i in range(0, 8):
            row_colors = []
            row_animation = []
            for j in range(0, 8):
                row_colors.append(definitions.BLACK)
                row_animation.append(push2_python.constants.ANIMATION_STATIC)
            color_matrix.append(row_colors)
            animation_matrix.append(row_animation)
        
        # Draw extra pads for notes (not the first note pad, these are drawn after to be always on top)
        for note_to_display in notes_to_display:
            pad_ij = note_to_display['pad_start_ij']
            for i in range(note_to_display['duration_n_pads']):
                if 0 <= pad_ij[0] <= 8 and 0 <= (pad_ij[1] + i) <= 7:
                    if i != 0:
                        color_matrix[pad_ij[0]][pad_ij[1] + i] = track_color + '_darker1'
        # Draw first-pads for notes (this will allow to always draw full color first-pad note for overlapping notes)
        for note_to_display in notes_to_display:
            pad_ij = note_to_display['pad_start_ij']
            if 0 <= pad_ij[0] <= 8 and 0 <= pad_ij[1] <= 7:
                color_matrix[pad_ij[0]][pad_ij[1]] = track_color

        return color_matrix, animation_matrix

    def quantize_helper(self):
        current_quantization_step = self.clip.currentquantizationstep
        if (current_quantization_step == 0.0):
            next_quantization_step = 4.0/16.0
        elif (current_quantization_step == 4.0/16.0):
            next_quantization_step = 4.0/8.0
        elif (current_quantization_step == 4.0/8.0):
            next_quantization_step = 4.0/4.0
        elif (current_quantization_step == 4.0/4.0):
            next_quantization_step = 0.0
        else:
            next_quantization_step = 0.0
        self.clip.quantize(next_quantization_step)

    def update_display(self, ctx, w, h):
        if not self.app.is_mode_active(self.app.settings_mode) and not self.app.is_mode_active(self.app.ddrm_tone_selector_mode):
            if self.selected_clip_uuid is not None:
                part_w = w // 8
                track_color = self.app.track_selection_mode.get_track_color(self.clip.track.order)
                track_color_rgb = definitions.get_color_rgb_float(track_color)
                
                # Slot 1, clip name
                show_title(ctx, part_w * 0, h, 'CLIP', color=track_color_rgb)
                show_value(ctx, part_w * 0, h, self.clip.name, color=track_color_rgb)

                # Slot 2, clip length
                show_title(ctx, part_w * 1, h, 'LENGTH')
                show_value(ctx, part_w * 1, h, '{:.1f}'.format(self.clip.cliplengthinbeats))

                # Slot 3, quantization
                show_title(ctx, part_w * 2, h, 'QUANTIZATION')
                quantization_step_labels = {
                    0.25: '16th note',
                    0.5: '8th note',
                    1.0: '4th note',
                    0.0: 'no quantization'
                }
                show_value(ctx, part_w * 2, h, '{}'.format(quantization_step_labels.get(self.clip.currentquantizationstep, self.clip.currentquantizationstep)))

                # Slot 4, view scale
                show_title(ctx, part_w * 3, h, 'VIEW SCALE')
                show_value(ctx, part_w * 3, h, '{:.3f}'.format(self.pads_pad_beat_scale))

                # Slots 5-8, clip preview
                if self.clip.cliplengthinbeats > 0.0:
                    draw_clip(ctx, self.clip, frame=(0.5, 0.0, 0.5, 0.87), event_color=track_color + '_darker1', highlight_color=track_color)
        
            beas_to_pad = self.beats_to_pad(self.clip.playheadpositioninbeats)
            if 0 <= beas_to_pad <= 7 and beas_to_pad is not self.last_beats_to_pad:
                # If clip is playing, trigger re-drawing pads when playhead position advances enough
                self.update_pads()

    def activate(self):
        self.update_buttons()
        self.update_pads()

        self.available_clips = []
        for track in self.app.shepherd_interface.session.tracks:
            for clip in track.clips:
                self.available_clips.append(clip.uuid)

    def deactivate(self):
        self.app.push.pads.set_all_pads_to_color(color=definitions.BLACK)
        for button_name in self.buttons_used:
            self.push.buttons.set_button_color(button_name, definitions.BLACK)

    def update_buttons(self):
        self.set_button_color_if_pressed(push2_python.constants.BUTTON_UPPER_ROW_1, animation=definitions.DEFAULT_ANIMATION)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_2, definitions.BLACK)
        self.set_button_color_if_pressed(push2_python.constants.BUTTON_UPPER_ROW_3, animation=definitions.DEFAULT_ANIMATION)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_4, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_5, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_6, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_7, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_8, definitions.BLACK)

        self.set_button_color_if_pressed(push2_python.constants.BUTTON_DOUBLE_LOOP, animation=definitions.DEFAULT_ANIMATION)
        self.set_button_color_if_pressed(push2_python.constants.BUTTON_QUANTIZE, animation=definitions.DEFAULT_ANIMATION)
        
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UP, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_DOWN, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_LEFT, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_RIGHT, definitions.WHITE)
            
    def update_pads(self):
        color_matrix, animation_matrix = self.notes_to_pads() 
        if self.clip.playing:
            # If clip is playing, draw playhead
            beats_to_pad = self.beats_to_pad(self.clip.playheadpositioninbeats)
            if 0 <= beats_to_pad <= 7:
                self.last_beats_to_pad = beats_to_pad
                for i in range(0, 8):
                    color_matrix[i][beats_to_pad] = definitions.WHITE
        self.push.pads.set_pads_color(color_matrix, animation_matrix)

    def on_button_pressed(self, button_name, shift=False, select=False, long_press=False, double_press=False):
        if button_name == push2_python.constants.BUTTON_UP:
            self.pads_min_note_offset += 1
            if self.pads_min_note_offset > 128 - 8:
                self.pads_min_note_offset = 128 - 8
            self.update_pads()
            return True
        elif button_name == push2_python.constants.BUTTON_DOWN:
            self.pads_min_note_offset -= 1
            if self.pads_min_note_offset < 0:
                self.pads_min_note_offset = 0
            self.update_pads()
            return True
        elif button_name == push2_python.constants.BUTTON_LEFT:
            self.pads_pad_beats_offset -= self.pads_pad_beat_scale
            if self.pads_pad_beats_offset < 0.0:
                self.pads_pad_beats_offset = 0.0
            self.update_pads()
            return True
        elif button_name == push2_python.constants.BUTTON_RIGHT:
            self.pads_pad_beats_offset += self.pads_pad_beat_scale
            # TODO: don't allow offset that would render clip invisible
            self.update_pads()
            return True
        elif button_name == push2_python.constants.BUTTON_DOUBLE_LOOP:
            self.clip.double()
            return True
        elif button_name == push2_python.constants.BUTTON_QUANTIZE:
            self.quantize_helper()
            return True
        elif button_name == push2_python.constants.BUTTON_UPPER_ROW_3:
            self.quantize_helper()
            return True

    def on_pad_pressed(self, pad_n, pad_ij, velocity, shift=False, select=False, long_press=False, double_press=False):
        notes_in_pad = self.notes_in_pad(pad_ij)
        if notes_in_pad:
            # Remove all notes
            for note in notes_in_pad:
                self.clip.remove_sequence_event(note.uuid)
        else:
            # Create a new note
            midi_note, timestamp = self.pad_ij_to_note_beat(pad_ij)
            self.clip.add_sequence_note_event(midi_note, velocity / 127, timestamp, self.pads_pad_beat_scale)
            if timestamp + self.pads_pad_beat_scale > self.clip.cliplengthinbeats:
                # If adding a not beyond current clip length
                self.clip.set_length(math.ceil(timestamp + self.pads_pad_beat_scale))
        return True

    def on_encoder_rotated(self, encoder_name, increment):
        if encoder_name == push2_python.constants.ENCODER_TRACK1_ENCODER:
            if self.available_clips:
                if self.selected_clip_uuid is not None:
                    try:
                        current_clip_index = self.available_clips.index(self.selected_clip_uuid)
                    except:
                        current_clip_index = None
                    if current_clip_index is None:
                        next_clip_index = 0
                    else:
                        next_clip_index = current_clip_index + increment
                        if next_clip_index < 0:
                            next_clip_index = 0
                        elif next_clip_index >= len(self.available_clips) - 1:
                            next_clip_index = len(self.available_clips) - 1
                    self.selected_clip_uuid = self.available_clips[next_clip_index]
                else:
                    self.selected_clip_uuid = self.available_clips[0]
            
        elif encoder_name == push2_python.constants.ENCODER_TRACK2_ENCODER:
            new_length = self.clip.cliplengthinbeats + increment
            if new_length < 1.0:
                new_length = 1.0
            self.clip.set_length(new_length)

        elif encoder_name == push2_python.constants.ENCODER_TRACK4_ENCODER:
            # Set pad beat zoom
            current_pad_scale = self.pads_pad_beat_scales.index(self.pads_pad_beat_scale)
            next_pad_scale = current_pad_scale + increment
            if next_pad_scale < 0:
                next_pad_scale = 0
            elif next_pad_scale >= len(self.pads_pad_beat_scales) - 1:
                next_pad_scale = self.pads_pad_beat_scales
            self.pads_pad_beat_scale = self.pads_pad_beat_scales[next_pad_scale]
            self.update_pads()

        return True  # Always return True because encoder should not be used in any other mode if this is first active
