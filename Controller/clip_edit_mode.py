import definitions
import push2_python
import time
import random

from display_utils import show_text, show_rectangle


class ClipEditgMode(definitions.ShepherdControllerMode):

    xor_group = 'pads'

    selected_clip_uuid = None
    
    buttons_used = []

    def update_display(self, ctx, w, h):
        if not self.app.is_mode_active(self.app.settings_mode) and not self.app.is_mode_active(self.app.ddrm_tone_selector_mode):
            if self.selected_clip_uuid is not None:
                clip = self.app.shepherd_interface.sss.get_element_with_uuid(self.selected_clip_uuid)
                show_text(ctx, 0, 20, "Editing clip\n{}".format(clip.name), center_horizontally=True)
                    
    def activate(self):
        self.update_buttons()
        self.update_pads()

    def deactivate(self):
        self.app.push.pads.set_all_pads_to_color(color=definitions.BLACK)

    def update_buttons(self):
        pass
            
    def update_pads(self):
        color_matrix = []
        animation_matrix = []
        for i in range(0, 8):
            row_colors = []
            row_animation = []
            for j in range(0, 8):
                row_colors.append(definitions.BLACK)
                row_animation.append(definitions.DEFAULT_ANIMATION)
            color_matrix.append(row_colors)
            animation_matrix.append(row_animation)
        self.push.pads.set_pads_color(color_matrix, animation_matrix)

    def on_button_pressed(self, button_name, shift=False, select=False, long_press=False, double_press=False):
        pass

    def on_pad_pressed(self, pad_n, pad_ij, velocity, shift=False, select=False, long_press=False, double_press=False):
        pass

    def on_encoder_rotated(self, encoder_name, increment):
        pass
