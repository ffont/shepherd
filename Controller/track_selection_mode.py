import definitions
import mido
import push2_python
import time
import math
import os
import json

from display_utils import show_text


class TrackSelectionMode(definitions.ShepherdControllerMode):

    devices_info = {}

    track_button_names = [
        push2_python.constants.BUTTON_LOWER_ROW_1,
        push2_python.constants.BUTTON_LOWER_ROW_2,
        push2_python.constants.BUTTON_LOWER_ROW_3,
        push2_python.constants.BUTTON_LOWER_ROW_4,
        push2_python.constants.BUTTON_LOWER_ROW_5,
        push2_python.constants.BUTTON_LOWER_ROW_6,
        push2_python.constants.BUTTON_LOWER_ROW_7,
        push2_python.constants.BUTTON_LOWER_ROW_8
    ]
    selected_track = 0

    def initialize(self, settings=None):
        if settings is not None:
            pass
        
        self.load_hardware_devices_info()

    def load_hardware_devices_info(self):
        """
        This method loads hardware device (aka instrument) definitions from definition files.
        These contain some information about the device which is useful to show a proper UI (for
        example, a list of midi CC parameter mappings).
        """
        print('Loading hardware device definitions...')
        for filename in os.listdir(definitions.DEVICE_DEFINITION_FOLDER):
            if filename.endswith('.json'):
                device_short_name = filename.replace('.json', '')
                self.devices_info[device_short_name] = json.load(open(os.path.join(definitions.DEVICE_DEFINITION_FOLDER, filename)))
                print('- {}'.format(device_short_name))

    def get_settings_to_save(self):
        return {}

    def get_all_distinct_device_short_names(self):
        return list(set([self.app.shepherd_interface.get_track_device_short_name(i) for i in range(0, self.app.shepherd_interface.get_num_tracks())]))

    def get_current_track_device_info(self):
        return self.devices_info[self.app.shepherd_interface.get_track_device_short_name(self.selected_track)]

    def get_track_device_short_name(self, i):
        return self.app.shepherd_interface.get_track_device_short_name(i)

    def get_current_track_device_short_name(self):
        return self.app.shepherd_interface.get_track_device_short_name(self.selected_track)

    def get_track_color(self, i):
        return definitions.COLORS_NAMES[i % 8]
    
    def get_current_track_color(self):
        return self.get_track_color(self.selected_track)

    def get_current_track_color_rgb(self):
        return definitions.get_color_rgb_float(self.get_current_track_color())
        
    def load_current_default_layout(self):
        if self.get_current_track_device_info().get('default_layout', definitions.LAYOUT_MELODIC) == definitions.LAYOUT_MELODIC:
            self.app.set_melodic_mode()
        elif self.get_current_track_device_info().get('default_layout', definitions.LAYOUT_MELODIC) == definitions.LAYOUT_RHYTHMIC:
            self.app.set_rhythmic_mode()
        elif self.get_current_track_device_info().get('default_layout', definitions.LAYOUT_MELODIC) == definitions.LAYOUT_SLICES:
            self.app.set_slice_notes_mode()

    def clean_currently_notes_being_played(self):
        if self.app.is_mode_active(self.app.melodic_mode):
            self.app.melodic_mode.remove_all_notes_being_played()
        elif self.app.is_mode_active(self.app.rhyhtmic_mode):
            self.app.rhyhtmic_mode.remove_all_notes_being_played()

    def send_select_track(self, track_idx):
        self.app.shepherd_interface.track_select(track_idx)

    def select_track(self, track_idx):
        # Selects a track
        # Note that if this is called from a mode from the same xor group with melodic/rhythmic modes,
        # that other mode will be deactivated.
        if track_idx < self.app.shepherd_interface.get_num_tracks():
            self.selected_track = track_idx
            self.send_select_track(self.selected_track)
            self.clean_currently_notes_being_played()
            try:
                self.app.midi_cc_mode.new_track_selected()
                self.app.preset_selection_mode.new_track_selected()
                self.app.clip_triggering_mode.new_track_selected()
            except AttributeError:
                # Might fail if MIDICCMode/PresetSelectionMode/ClipTriggeringMode not initialized
                pass
            self.app.shepherd_interface.track_set_active_ui_notes_monitoring(track_idx)
            
    def activate(self):
        self.update_buttons()
        self.update_pads()
        self.select_track(self.selected_track)

    def deactivate(self):
        for button_name in self.track_button_names:
            self.push.buttons.set_button_color(button_name, definitions.BLACK)

    def update_buttons(self):
        for count, name in enumerate(self.track_button_names):
            color = self.get_track_color(count)
            self.push.buttons.set_button_color(name, color)

    def update_display(self, ctx, w, h):
        # Draw track selector labels
        height = 20
        for i in range(0, self.app.shepherd_interface.get_num_tracks()):
            track_color = self.get_track_color(i)
            if self.selected_track == i:
                background_color = track_color
                font_color = definitions.BLACK
            else:
                background_color = definitions.BLACK
                font_color = track_color
            device_short_name = self.get_track_device_short_name(i)
            if self.app.shepherd_interface.get_track_is_input_monitoring(i):
                device_short_name = '+' + device_short_name
            show_text(ctx, i, h - height, device_short_name, height=height,
                      font_color=font_color, background_color=background_color)

    def on_button_pressed(self, button_name, shift=False, select=False, long_press=False, double_press=False):
       if button_name in self.track_button_names:
            track_idx = self.track_button_names.index(button_name)
            if long_press:
                # Toggle input monitoring
                if self.app.shepherd_interface.get_track_is_input_monitoring(track_idx):
                    self.app.shepherd_interface.track_set_input_monitoring(track_idx, False)
                else:
                    self.app.shepherd_interface.track_set_input_monitoring(track_idx, True)
            else:
                if not shift:
                    # If button shift not pressed, select the track
                    self.select_track(self.track_button_names.index(button_name))
                else:
                    # If button shift pressed, send all notes off to that track
                    self.app.shepherd_interface.device_send_all_notes_off(self.get_track_device_short_name(track_idx))
