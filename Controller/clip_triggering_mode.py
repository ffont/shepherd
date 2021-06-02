import definitions
import push2_python
import time

from display_utils import show_text


class ClipTriggeringMode(definitions.ShepherdControllerMode):

    xor_group = 'pads'

    selected_scene = 0
    num_scenes = 8

    scene_trigger_buttons = [
        push2_python.constants.BUTTON_1_32T,
        push2_python.constants.BUTTON_1_32,
        push2_python.constants.BUTTON_1_16T,
        push2_python.constants.BUTTON_1_16,
        push2_python.constants.BUTTON_1_8T,
        push2_python.constants.BUTTON_1_8,
        push2_python.constants.BUTTON_1_4T,
        push2_python.constants.BUTTON_1_4
    ]
    clear_clip_button = push2_python.constants.BUTTON_DELETE
    double_clip_button = push2_python.constants.BUTTON_DOUBLE_LOOP
    quantize_button = push2_python.constants.BUTTON_QUANTIZE
    undo_button = push2_python.constants.BUTTON_UNDO
    duplicate_button = push2_python.constants.BUTTON_DUPLICATE

    buttons_used = scene_trigger_buttons + [clear_clip_button, double_clip_button, quantize_button, undo_button, duplicate_button]

    def update_display(self, ctx, w, h):
        # Draw clip progress bars
        info_to_draw = []
        accumulated_clip_index = 0
        for track_num in range(0, self.app.shepherd_interface.get_num_tracks()):
            current_track_playing_clips_info = []
            for clip_num in range(0, self.app.shepherd_interface.get_track_num_clips(track_num)):
                clip_state = self.app.shepherd_interface.get_clip_state(track_num, clip_num)
                if 'p' in clip_state or 'C' in clip_state:
                    clip_length = float(clip_state.split('|')[1])
                    playhead_position = self.app.shepherd_interface.get_clip_playhead(accumulated_clip_index)
                    current_track_playing_clips_info.append((clip_num, clip_length, playhead_position))
                accumulated_clip_index += 1
            if current_track_playing_clips_info:
                info_to_draw.append((track_num, current_track_playing_clips_info))

        for track_num, playing_clips in info_to_draw:
            num_clips = len(playing_clips)  # There should normally be only 1 clip playing per track at a time, but this supports multiple clips playing
            for i , (clip_num, clip_length, playhead_position) in enumerate(playing_clips):
                height = (h - 20) // num_clips
                y = height * i
                track_color = self.app.track_selection_mode.get_track_color(track_num)
                background_color = track_color
                font_color = track_color + '_darker1'
                if clip_length > 0.0:
                    position_percentage = playhead_position/clip_length
                else:
                    position_percentage = 0.0
                show_text(ctx, track_num, y, str(playhead_position), height=height, font_color=font_color, background_color=background_color, font_size_percentage=0.35 if num_clips > 1 else 0.2, rectangle_width_percentage=position_percentage, center_horizontally=True)

    def activate(self):
        self.update_buttons()
        self.update_pads()

    def new_track_selected(self):
        self.app.pads_need_update = True
        self.app.buttons_need_update = True

    def update_buttons(self):
        for i, button_name in enumerate(self.scene_trigger_buttons):
            self.set_button_color_if_expression(button_name, self.selected_scene == i, definitions.GREEN, false_color=definitions.WHITE)
        self.set_button_color_if_pressed(self.clear_clip_button, animation=definitions.DEFAULT_ANIMATION)
        self.set_button_color_if_pressed(self.double_clip_button, animation=definitions.DEFAULT_ANIMATION)
        self.set_button_color_if_pressed(self.quantize_button, animation=definitions.DEFAULT_ANIMATION)
        self.set_button_color_if_pressed(self.undo_button, animation=definitions.DEFAULT_ANIMATION)
        self.set_button_color(self.duplicate_button)

    def update_pads(self):
        color_matrix = []
        animation_matrix = []
        for i in range(0, 8):
            row_colors = []
            row_animation = []
            for j in range(0, 8):
                state = self.app.shepherd_interface.get_clip_state(j, i)

                track_color = self.app.track_selection_mode.get_track_color(j)
                cell_animation = 0

                if 'E' in state:
                    # Is empty
                    cell_color = definitions.BLACK
                else:
                    cell_color = track_color + '_darker1'

                if 'p' in state:
                    # Is playing
                    cell_color = track_color

                if 'c' in state or 'C' in state:
                    # Will start or will stop playing
                    cell_color = track_color
                    cell_animation = definitions.DEFAULT_ANIMATION

                if 'w' in state or 'W' in state:
                    # Will start or will stop recording
                    cell_color = definitions.RED
                    cell_animation = definitions.DEFAULT_ANIMATION

                if 'r' in state:
                    # Is recording
                    cell_color = definitions.RED

                row_colors.append(cell_color)
                row_animation.append(cell_animation)
            color_matrix.append(row_colors)
            animation_matrix.append(row_animation)
        self.push.pads.set_pads_color(color_matrix, animation_matrix)

    def on_button_pressed(self, button_name, shift=False, select=False, long_press=False, double_press=False):
        if button_name in self.scene_trigger_buttons:
            triggered_scene_row = self.scene_trigger_buttons.index(button_name)
            self.app.shepherd_interface.scene_play(triggered_scene_row)
            self.selected_scene = triggered_scene_row
            self.app.buttons_need_update = True
            return True

        elif button_name == self.duplicate_button:
            if self.selected_scene < self.num_scenes - 1:
                # Do not duplicate scene if we're at the last one (no more space!)
                self.app.shepherd_interface.scene_duplicate(self.selected_scene)
                self.selected_scene += 1
                self.app.buttons_need_update = True
            return True

    def on_pad_pressed(self, pad_n, pad_ij, velocity, shift=False, select=False, long_press=False, double_press=False):
        track_num = pad_ij[1]
        clip_num = pad_ij[0]

        if self.app.is_button_being_pressed(self.app.main_controls_mode.record_button):
            # Toggle record on/off for that clip if record button is being pressed
            self.app.shepherd_interface.clip_record_on_off(track_num, clip_num)
            self.app.set_button_ignore_next_action_if_not_yet_triggered(self.app.main_controls_mode.record_button)

        else:
            if self.app.is_button_being_pressed(self.clear_clip_button):
                self.app.shepherd_interface.clip_clear(track_num, clip_num)

            elif self.app.is_button_being_pressed(self.double_clip_button):
                self.app.shepherd_interface.clip_double(track_num, clip_num)

            elif self.app.is_button_being_pressed(self.quantize_button):
                self.app.shepherd_interface.clip_quantize(track_num, clip_num)

            elif self.app.is_button_being_pressed(self.undo_button):
                self.app.shepherd_interface.clip_undo(track_num, clip_num)

            else:
                # No "option" button pressed, do play/stop
                self.app.shepherd_interface.clip_play_stop(track_num, clip_num)
