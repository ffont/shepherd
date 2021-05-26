import definitions
import push2_python
import time


class ClipTriggeringMode(definitions.ShepherdControllerMode):

    xor_group = 'pads'

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
    
    clear_clip_button_being_pressed = False
    clear_clip_button = push2_python.constants.BUTTON_DELETE

    double_clip_button_being_pressed = False
    double_clip_button = push2_python.constants.BUTTON_DOUBLE_LOOP

    quantize_button_being_pressed = False
    quantize_button = push2_python.constants.BUTTON_QUANTIZE

    undo_button_being_pressed = False
    undo_button = push2_python.constants.BUTTON_UNDO

    times_pad_pressed = {}
    ignore_next_pad_release = {}
    pad_pressing_action_time = 0.5

    def activate(self):
        self.clear_clip_button_being_pressed = False
        self.double_clip_button_being_pressed = False
        self.update_buttons()
        self.update_pads()

    def new_track_selected(self):
        self.clear_clip_button_being_pressed = False
        self.double_clip_button_being_pressed = False
        self.app.pads_need_update = True
        self.app.buttons_need_update = True

    def deactivate(self):
        for button_name in self.scene_trigger_buttons:
            self.push.buttons.set_button_color(button_name, definitions.BLACK)
        self.app.push.pads.set_all_pads_to_color(color=definitions.BLACK)
        self.push.buttons.set_button_color(push2_python.constants.BUTTON_DUPLICATE, definitions.BLACK)
        self.push.buttons.set_button_color(self.clear_clip_button, definitions.BLACK)
        self.push.buttons.set_button_color(self.double_clip_button, definitions.BLACK)
        self.push.buttons.set_button_color(self.quantize_button, definitions.BLACK)
        self.push.buttons.set_button_color(self.undo_button, definitions.BLACK)

    def update_buttons(self):
        for i, button_name in enumerate(self.scene_trigger_buttons):
            if self.app.shepherd_interface.get_selected_scene() == i:
                self.push.buttons.set_button_color(button_name, definitions.GREEN)
            else:
                self.push.buttons.set_button_color(button_name, definitions.WHITE)
        
        if not self.clear_clip_button_being_pressed:
            self.push.buttons.set_button_color(self.clear_clip_button, definitions.OFF_BTN_COLOR)
        else:
            self.push.buttons.set_button_color(self.clear_clip_button, definitions.WHITE, animation=definitions.DEFAULT_ANIMATION)

        self.push.buttons.set_button_color(push2_python.constants.BUTTON_DUPLICATE, definitions.OFF_BTN_COLOR)        

        if not self.double_clip_button_being_pressed:
            self.push.buttons.set_button_color(self.double_clip_button, definitions.OFF_BTN_COLOR)
        else:
            self.push.buttons.set_button_color(self.double_clip_button, definitions.WHITE, animation=definitions.DEFAULT_ANIMATION)

        if not self.quantize_button_being_pressed:
            self.push.buttons.set_button_color(self.quantize_button, definitions.OFF_BTN_COLOR)
        else:
            self.push.buttons.set_button_color(self.quantize_button, definitions.WHITE, animation=definitions.DEFAULT_ANIMATION)

        if not self.undo_button_being_pressed:
            self.push.buttons.set_button_color(self.undo_button, definitions.OFF_BTN_COLOR)
        else:
            self.push.buttons.set_button_color(self.undo_button, definitions.WHITE, animation=definitions.DEFAULT_ANIMATION)


    def update_pads(self):
        # Update pads according to clip state
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

    def on_button_pressed(self, button_name):
        if button_name in self.scene_trigger_buttons:
            triggered_scene_row = self.scene_trigger_buttons.index(button_name)
            self.app.shepherd_interface.scene_play(triggered_scene_row)
            return True  # Prevent other modes to get this event

        elif button_name == self.clear_clip_button:
            self.clear_clip_button_being_pressed = True
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == self.double_clip_button:
            self.double_clip_button_being_pressed = True
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == self.quantize_button:
            self.quantize_button_being_pressed = True
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == self.undo_button:
            self.undo_button_being_pressed = True
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == push2_python.constants.BUTTON_DUPLICATE:
            self.app.shepherd_interface.scene_duplicate(self.app.shepherd_interface.get_selected_scene())
            return True  # Prevent other modes to get this event

    def on_button_released(self, button_name):

        if button_name == self.clear_clip_button:
            self.clear_clip_button_being_pressed = False
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event
            
        elif button_name == self.double_clip_button:
            self.double_clip_button_being_pressed = False
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == self.quantize_button:
            self.quantize_button_being_pressed = False
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

        elif button_name == self.undo_button:
            self.undo_button_being_pressed = False
            self.app.buttons_need_update = True
            return True  # Prevent other modes to get this event

    def on_pad_pressed(self, pad_n, pad_ij, velocity):
        if self.clear_clip_button_being_pressed:
            # Send clip clear in shepherd
            self.app.shepherd_interface.clip_clear(pad_ij[1], pad_ij[0])
            self.ignore_next_pad_release[pad_n] = True

        elif self.double_clip_button_being_pressed:
            # Send clip double in shepherd
            self.app.shepherd_interface.clip_double(pad_ij[1], pad_ij[0])
            self.ignore_next_pad_release[pad_n] = True

        elif self.quantize_button_being_pressed:
            # Send clip quantize in shepherd
            self.app.shepherd_interface.clip_quantize(pad_ij[1], pad_ij[0])
            self.ignore_next_pad_release[pad_n] = True

        elif self.undo_button_being_pressed:
            # Send clip undo in shepherd
            self.app.shepherd_interface.clip_undo(pad_ij[1], pad_ij[0])
            self.ignore_next_pad_release[pad_n] = True

        # NOTE: the clip play/stop actions are sent on pad release, in this way we can distinguish long vs short presses
        self.times_pad_pressed[pad_n] = time.time()

    def on_pad_released(self, pad_n, pad_ij, velocity):
        if not self.clear_clip_button_being_pressed and not self.double_clip_button_being_pressed and not self.quantize_button_being_pressed and not self.undo_button_being_pressed:
            if not self.ignore_next_pad_release.get(pad_n, False):
                last_time_pressed = self.times_pad_pressed.get(pad_n, 0)
                pressing_time = time.time() - last_time_pressed
                self.times_pad_pressed[pad_n] = 0
                if pressing_time > self.pad_pressing_action_time:
                    # Long press, toggle recording
                    self.app.shepherd_interface.clip_record_on_off(pad_ij[1], pad_ij[0])
                else:
                    # Short press, send play/stop
                    self.app.shepherd_interface.clip_play_stop(pad_ij[1], pad_ij[0])
            
        if self.ignore_next_pad_release.get(pad_n, False):
            self.ignore_next_pad_release[pad_n] = False  # Revert to false so next time pad release is triggered normally
