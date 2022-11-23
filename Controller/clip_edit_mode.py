import definitions
import push2_python
import time
import random
import math

from display_utils import show_text, show_rectangle


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
    ]

    pads_min_note_offset = 64
    pads_pad_beats_offset = 0.0 # Offset for notes to be shown
    pads_pad_beat_scale = 0.5 # Default, 1 pad is one half of a beat, there fore 8 pads are 1 bar (assuming 4/4)

    pads_pad_beat_scales = [0.125 + 0.125 * i for i in range(0, 32)]

    '''
    Slot 1 = select clip
    Slot 2 = clip length
    Slot 3 = clip quanitzation
    Slot 4 = double clip action
    Slots 5-8 = clip preview 
    '''

    @property
    def clip(self):
        if self.selected_clip_uuid is not None:
            return self.app.shepherd_interface.sss.get_element_with_uuid(self.selected_clip_uuid)
        else:
            return None

    def notes_to_pads(self):
        # TODO: better check if note starts/ends within displayed section or starts before and ends before...
        notes = [event for event in self.clip.sequence_events if event.type == 1 and 
                                                                 (self.pads_pad_beats_offset <= event.renderedstarttimestamp < self.pads_pad_beats_offset + (self.pads_pad_beat_scale * 8)) or (self.pads_pad_beats_offset <= event.renderedendtimestamp < self.pads_pad_beats_offset + (self.pads_pad_beat_scale * 8)) and
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
        for note_to_display in notes_to_display:
            pad_ij = note_to_display['pad_start_ij']
            for i in range(note_to_display['duration_n_pads']):
                color_matrix[pad_ij[0]][min(pad_ij[1] + i, 7)] = track_color

        self.push.pads.set_pads_color(color_matrix, animation_matrix)

    def draw_clip(self, 
                  clip, 
                  ctx, 
                  frame=(0.0, 0.0, 1.0, 1.0), 
                  event_color=definitions.WHITE, 
                  highlight_color=definitions.GREEN, 
                  highlight_active_notes=True, 
                  background_color=None
                  ):
        xoffset_percentage = frame[0]
        yoffset_percentage = frame[1]
        width_percentage = frame[2] 
        height_percentage = frame[3]
        display_w = push2_python.constants.DISPLAY_LINE_PIXELS
        display_h = push2_python.constants.DISPLAY_N_LINES
        x = display_w * xoffset_percentage
        y = display_h * (1.0 - (yoffset_percentage + height_percentage))
        width = display_w * width_percentage
        height = display_h * height_percentage

        if background_color is not None:
            show_rectangle(ctx, xoffset_percentage, yoffset_percentage, width_percentage, height_percentage, background_color=background_color)
        
        rendered_notes = [event for event in self.clip.sequence_events if event.type == 1 and event.renderedstarttimestamp >= 0.0]
        all_midinotes = [int(note.midinote) for note in rendered_notes]
        playhead_position_percentage = clip.playheadpositioninbeats/clip.cliplengthinbeats

        if len(all_midinotes) > 0:
            min_midinote = min(all_midinotes)
            max_midinote = max(all_midinotes) + 1  # Add 1 to highest note does not fall outside of screen
            for note in rendered_notes:
                note_percentage =  (int(note.midinote) - min_midinote) / (max_midinote - min_midinote)
                note_height_percentage =  height / (max_midinote - min_midinote)
                note_start_percentage = float(note.renderedstarttimestamp) / clip.cliplengthinbeats
                note_end_percentage = float(note.renderedendtimestamp) / clip.cliplengthinbeats
                if note_start_percentage <= note_end_percentage:  
                    # Note does not wrap across clip boundaries, draw 1 rectangle  
                    if (note_start_percentage <= playhead_position_percentage <= note_end_percentage + 0.05) and clip.playheadpositioninbeats != 0.0: 
                        color = highlight_color
                    else:
                        color = event_color
                    x0_rel = (x + note_start_percentage * width) / display_w
                    y0_rel = (y - note_percentage * height + note_height_percentage) / display_h
                    width_rel = ((x + note_end_percentage * width) / display_w) - x0_rel
                    height_rel = note_height_percentage / display_h
                    show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)
                else:
                    # Draw "2 rectangles", one from start of note to end of section, and one from start of section to end of note
                    if (note_start_percentage <= playhead_position_percentage or playhead_position_percentage <= note_end_percentage + 0.05) and clip.playheadpositioninbeats != 0.0: 
                        color = highlight_color
                    else:
                        color = event_color

                    x0_rel = (x + note_start_percentage * width) / display_w
                    y0_rel = (y - note_percentage * height + note_height_percentage) / display_h
                    width_rel = ((x + 1.0 * width) / display_w) - x0_rel
                    height_rel = note_height_percentage / display_h
                    show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)

                    x0_rel = (x + note_start_percentage * width) / display_w
                    y0_rel = (y - note_percentage * height + note_height_percentage) / display_h
                    width_rel = ((x + note_end_percentage * width) / display_w) - x0_rel
                    height_rel = note_height_percentage / display_h
                    show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)

    def update_display(self, ctx, w, h):
        if not self.app.is_mode_active(self.app.settings_mode) and not self.app.is_mode_active(self.app.ddrm_tone_selector_mode):
            if self.selected_clip_uuid is not None:

                track_color = self.app.track_selection_mode.get_track_color(self.clip.track.order)
                
                # Slot 1, clip name
                show_text(ctx, 0, 20, "Editing clip\n{}".format(self.clip.name), center_horizontally=True)

                # Slots 5-8, clip preview
                if self.clip.cliplengthinbeats > 0.0:
                    self.draw_clip(self.clip, ctx, frame=(0.5, 0.0, 0.5, 0.87), event_color=track_color + '_darker1', highlight_color=track_color, background_color=definitions.WHITE)

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
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_1, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_2, definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_3, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_4, definitions.WHITE)

        self.push.buttons.set_button_color(push2_python.constants.BUTTON_UP, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_DOWN, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_LEFT, definitions.WHITE)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_RIGHT, definitions.WHITE)
            
    def update_pads(self):
        self.notes_to_pads()

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
            # TODO: don't allow ofsset that would render clip invisible
            self.update_pads()
            return True

    def on_pad_pressed(self, pad_n, pad_ij, velocity, shift=False, select=False, long_press=False, double_press=False):
        pass

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
            # TODO: set clip length
            pass

        elif encoder_name == push2_python.constants.ENCODER_TRACK3_ENCODER:
            # TODO: cycle quantization settings
            pass

        elif encoder_name == push2_python.constants.ENCODER_TRACK4_ENCODER:
            # TODO: double clip action
            pass

        elif encoder_name == push2_python.constants.ENCODER_TRACK5_ENCODER:
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
