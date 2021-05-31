import definitions
import mido
import push2_python
import time
import math
import os
import json

from display_utils import show_text


class TrackSelectionMode(definitions.ShepherdControllerMode):

    tracks_info = []
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
        
        self.create_tracks()

    def create_tracks(self):
        """
        This method creates tracks according to "track_listing.json" file. This is some sort of temporary workaround
        have tracks defined while the concep of track is not further developed in Shepherd. Each tracj position
        should correspond to the MIDI channel fo the device. Instrument names per track are loaded from "track_listing.json" file, 
        and should correspond to instrument definition filenames from "instrument_definitions" folder.
        """
        tmp_instruments_data = {}

        if os.path.exists(definitions.TRACK_LISTING_PATH):
            track_instruments = json.load(open(definitions.TRACK_LISTING_PATH))
            for i, instrument_short_name in enumerate(track_instruments):
                if instrument_short_name not in tmp_instruments_data:
                    try:
                        instrument_data = json.load(open(os.path.join(definitions.INSTRUMENT_DEFINITION_FOLDER, '{}.json'.format(instrument_short_name))))
                        tmp_instruments_data[instrument_short_name] = instrument_data
                    except FileNotFoundError:
                        # No definition file for instrument exists
                        instrument_data = {}
                else:
                    instrument_data = tmp_instruments_data[instrument_short_name]
                color = instrument_data.get('color', None)
                if color is None:
                    if instrument_short_name != '-':
                        color = definitions.COLORS_NAMES[i % 8]
                    else:
                        color = definitions.GRAY_DARK
                self.tracks_info.append({
                    'track_name': '{0}{1}'.format((i % 16) + 1, ['A', 'B', 'C', 'D'][i//16]),
                    'instrument_name': instrument_data.get('instrument_name', '-'),
                    'instrument_short_name': instrument_short_name,
                    'midi_channel': instrument_data.get('midi_channel', -1),
                    'color': color,
                    'n_banks': instrument_data.get('n_banks', 1),
                    'bank_names': instrument_data.get('bank_names', None),
                    'default_layout': instrument_data.get('default_layout', definitions.LAYOUT_MELODIC),
                    'illuminate_local_notes': instrument_data.get('illuminate_local_notes', True), 
                })
            print('Created {0} tracks!'.format(len(self.tracks_info)))

    def get_settings_to_save(self):
        return {}

    def get_all_distinct_instrument_short_names(self):
        return list(set([track['instrument_short_name'] for track in self.tracks_info]))

    def get_current_track_info(self):
        return self.tracks_info[self.selected_track]

    def get_current_track_instrument_short_name(self):
        return self.get_current_track_info()['instrument_short_name']

    def get_track_color(self, i):
        return self.tracks_info[i]['color']
    
    def get_current_track_color(self):
        return self.get_track_color(self.selected_track)

    def get_current_track_color_rgb(self):
        return definitions.get_color_rgb_float(self.get_current_track_color())
        
    def load_current_default_layout(self):
        if self.get_current_track_info()['default_layout'] == definitions.LAYOUT_MELODIC:
            self.app.set_melodic_mode()
        elif self.get_current_track_info()['default_layout'] == definitions.LAYOUT_RHYTHMIC:
            self.app.set_rhythmic_mode()
        elif self.get_current_track_info()['default_layout'] == definitions.LAYOUT_SLICES:
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
        
    def activate(self):
        self.update_buttons()
        self.update_pads()
        self.select_track(self.selected_track)

    def deactivate(self):
        for button_name in self.track_button_names:
            self.push.buttons.set_button_color(button_name, definitions.BLACK)

    def update_buttons(self):
        for count, name in enumerate(self.track_button_names):
            color = self.tracks_info[count]['color']
            self.push.buttons.set_button_color(name, color)

    def update_display(self, ctx, w, h):
        # Draw track selector labels
        height = 20
        for i in range(0, self.app.shepherd_interface.get_num_tracks()):
            track_color = self.tracks_info[i]['color']
            if self.selected_track == i:
                background_color = track_color
                font_color = definitions.BLACK
            else:
                background_color = definitions.BLACK
                font_color = track_color
            instrument_short_name = self.tracks_info[i]['instrument_short_name']
            if self.app.shepherd_interface.get_track_is_input_monitoring(i):
                instrument_short_name = '+' + instrument_short_name
            show_text(ctx, i, h - height, instrument_short_name, height=height,
                      font_color=font_color, background_color=background_color)
 
    def on_button_pressed(self, button_name):
        if button_name in self.track_button_names:
            track_idx = self.track_button_names.index(button_name)
            if not self.app.main_controls_mode.shift_button_pressed:
                # If button shift not pressed, select the track
                self.select_track(self.track_button_names.index(button_name))
            else:
                # If shift button is being pressed, toggle insput monitoring for that track
                if self.app.shepherd_interface.get_track_is_input_monitoring(track_idx):
                    self.app.shepherd_interface.track_set_input_monitoring(track_idx, False)
                else:
                    self.app.shepherd_interface.track_set_input_monitoring(track_idx, True)


                # TODO: send notes off?
                #msg = mido.Message('control_change', control=120 - 1, value=0, channel=self.get_current_track_info()['midi_channel'] - 1)
                #self.app.send_midi(msg)


