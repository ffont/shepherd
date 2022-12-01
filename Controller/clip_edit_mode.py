import definitions
import push2_python
import time
import random
import math

from display_utils import show_title, show_value, draw_clip
from utils import clamp, clamp01


class GeneratorAlogorithm(object):
    parameters = []
    name = ''

    def __init__(self):
        for key, param_data in self.parameters.items():
            param_data.update({'name': key, 'value': param_data['default']})

    def get_algorithm_parameters(self):
        return list(self.parameters.values())

    def generate_sequence(self):
        raise NotImplementedError

    def update_parameter_value(self, name, increment):
        # increment will be the increment sent form the encoder moving. small increments are -1/+1
        self.parameters[name]['value'] = clamp(
            self.parameters[name]['value'] + increment * self.parameters[name]['increment_scale'],
            self.parameters[name]['min'],
            self.parameters[name]['max'],
        )


class RandomGeneratorAlgorithm(GeneratorAlogorithm):
    name = 'Rnd'
    parameters = {
        'length': {'display_name': 'LENGTH', 'type': float, 'min': 1.0, 'max': 32.0, 'default': 8.0, 'increment_scale': 1.0},
        'density': {'display_name': 'DENSITY', 'type': int, 'min': 1, 'max': 15, 'default': 5, 'increment_scale': 1},
    }

    def generate_sequence(self):
        if self.parameters['length']['value'] > 0.0:
            new_clip_length = self.parameters['length']['value']
        else:
            new_clip_length = random.randint(5, 13)
        random_sequence = []
        for i in range(0, abs(self.parameters['density']['value'] + random.randint(-2, 2))):
            timestamp = (new_clip_length - 0.5) * random.random()
            duration = random.random() * 1.5 + 0.01
            random_sequence.append(
                {'type': 1, 'midiNote': random.randint(64, 85), 'midiVelocity': 1.0, 'timestamp': timestamp, 'duration': duration}
            )
        return random_sequence, new_clip_length


class RandomGeneratorAlgorithmPlus(GeneratorAlogorithm):
    name = 'Rnd+'
    parameters = {
        'length': {'display_name': 'LENGTH', 'type': float, 'min': 1.0, 'max': 32.0, 'default': 8.0, 'increment_scale': 1.0},
        'density': {'display_name': 'DENSITY', 'type': int, 'min': 1, 'max': 15, 'default': 5, 'increment_scale': 1},
        'max_duration': {'display_name': 'MAX DUR', 'type': float, 'min': 0.1, 'max': 10.0, 'default': 0.5, 'increment_scale': 0.125},
    }

    def generate_sequence(self):
        if self.parameters['length']['value'] > 0.0:
            new_clip_length = self.parameters['length']['value']
        else:
            new_clip_length = random.randint(5, 13)
        random_sequence = []
        for i in range(0, abs(self.parameters['density']['value'] + random.randint(-2, 2))):
            timestamp = (new_clip_length - 0.5) * random.random()
            duration = max(0.1, random.random() * self.parameters['max_duration']['value'])
            random_sequence.append(
                {'type': 1, 'midiNote': random.randint(64, 85), 'midiVelocity': 1.0, 'timestamp': timestamp, 'duration': duration}
            )
        return random_sequence, new_clip_length


class ClipEditgMode(definitions.ShepherdControllerMode):

    xor_group = 'pads'
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
        push2_python.constants.BUTTON_DELETE,
        push2_python.constants.BUTTON_RECORD,
        push2_python.constants.BUTTON_CLIP,
    ]

    MODE_CLIP = 'mode_clip'
    MODE_EVENT = 'mdoe_event'
    MODE_GENERATOR = 'mode_generator'
    mode = MODE_CLIP

    selected_clip_uuid = None
    available_clips = []

    selected_event_uuid = None

    generator_algorithms = []
    selected_generator_algorithm = 0

    pads_min_note_offset = 64
    pads_pad_beats_offset = 0.0 # Offset for notes to be shown
    pads_pad_beat_scale = 0.5 # Default, 1 pad is one half of a beat, there fore 8 pads are 1 bar (assuming 4/4)
    pads_pad_beat_scales = [0.125 + 0.125 * i for i in range(0, 32)]

    last_beats_to_pad = -1

    '''
    MODE_CLIP
    Slot 1 = select clip (Slot 1 button triggers clip play/stop)
    Slot 2 = clip length
    Slot 3 = quantization
    Slot 4 = view scale
    Slots 5-8 = clip preview 

    MODE_EVENT
    Slot 1 = midi note
    Slot 2 = duration (rotating ecoder sets quantized duration, encoder + shift sets without quantization)
    Slot 3 = utime
    Slot 4 = chance

    MODE_GENERATOR
    Slot 1 = algorithm (Slot 1 button triggers generation)
    Slot 2-x = allgorithm paramters
    '''

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.generator_algorithms = [
            RandomGeneratorAlgorithm(),
            RandomGeneratorAlgorithmPlus()
        ]

    @property
    def clip(self):
        if self.selected_clip_uuid is not None:
            return self.app.shepherd_interface.sss.get_element_with_uuid(self.selected_clip_uuid)
        else:
            return None

    @property
    def event(self):
        if self.selected_event_uuid is not None:
            try:
                return self.app.shepherd_interface.sss.get_element_with_uuid(self.selected_event_uuid)
            except KeyError:
                return None
        else:
            return None

    @property
    def generator_algorithm(self):
        return self.generator_algorithms[self.selected_generator_algorithm]

    @property
    def start_displayed_time(self):
        return self.pads_pad_beats_offset

    @property
    def end_displayed_time(self):
        return self.pads_pad_beats_offset + self.pads_pad_beat_scale * 8

    def adjust_pads_to_sequence(self):
        # Auto adjust pads_min_note_offset, etc
        if self.clip.sequence_events:
            self.pads_min_note_offset = min([event.midinote for event in self.clip.sequence_events if event.is_type_note()])
        else:
            self.pads_min_note_offset = 64
        self.pads_pad_beats_offset = 0.0
        self.pads_pad_beat_scale = 0.5

    def set_clip_mode(self, new_clip_uuid):
        self.selected_event_uuid = None
        self.selected_clip_uuid = new_clip_uuid
        self.adjust_pads_to_sequence()
        self.mode = self.MODE_CLIP

    def set_event_mode(self, new_event_uuid):
        self.selected_event_uuid = new_event_uuid
        self.mode = self.MODE_EVENT

    def pad_ij_to_note_beat(self, pad_ij):
        note = self.pads_min_note_offset + (7 - pad_ij[0])
        beat = pad_ij[1] * self.pads_pad_beat_scale + self.pads_pad_beats_offset
        return note, beat

    def notes_in_pad(self, pad_ij):
        midi_note, start_time = self.pad_ij_to_note_beat(pad_ij)
        end_time = start_time + self.pads_pad_beat_scale
        notes = [event for event in self.clip.sequence_events if event.is_type_note() and 
                                                                 start_time <= event.renderedstarttimestamp <= end_time and
                                                                 event.midinote == midi_note]
        return notes

    def beats_to_pad(self, beats):
        return int(math.floor(8 * (beats - self.start_displayed_time)/(self.end_displayed_time - self.start_displayed_time)))

    def notes_to_pads(self):
        notes = [event for event in self.clip.sequence_events if event.is_type_note() and 
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
                'is_selected_in_note_edit_mode': event.uuid == self.selected_event_uuid
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
                        if not note_to_display['is_selected_in_note_edit_mode']:
                            color_matrix[pad_ij[0]][pad_ij[1] + i] = track_color + '_darker1'
                        else:
                            color_matrix[pad_ij[0]][pad_ij[1] + i] = definitions.GRAY_DARK
        # Draw first-pads for notes (this will allow to always draw full color first-pad note for overlapping notes)
        for note_to_display in notes_to_display:
            pad_ij = note_to_display['pad_start_ij']
            if 0 <= pad_ij[0] <= 8 and 0 <= pad_ij[1] <= 7:
                color_matrix[pad_ij[0]][pad_ij[1]] = track_color
                if note_to_display['is_selected_in_note_edit_mode']:
                    animation_matrix[pad_ij[0]][pad_ij[1]] = definitions.DEFAULT_ANIMATION
                    color_matrix[pad_ij[0]][pad_ij[1]] = definitions.WHITE

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

    def set_new_generated_sequence(self):
        random_sequence, new_clip_length = self.generator_algorithm.generate_sequence()
        self.clip.set_sequence({
                'clipLength': new_clip_length,
                'sequenceEvents': random_sequence,
        })
        self.adjust_pads_to_sequence()

    def update_display(self, ctx, w, h):
        if not self.app.is_mode_active(self.app.settings_mode) and not self.app.is_mode_active(self.app.ddrm_tone_selector_mode):
            part_w = w // 8
            track_color = self.app.track_selection_mode.get_track_color(self.clip.track.order)
            track_color_rgb = definitions.get_color_rgb_float(track_color)

            if self.mode == self.MODE_CLIP:
                if self.selected_clip_uuid is not None:
                    
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
                        0.0: '-'
                    }
                    show_value(ctx, part_w * 2, h, '{}'.format(quantization_step_labels.get(self.clip.currentquantizationstep, self.clip.currentquantizationstep)))

                    # Slot 4, view scale
                    show_title(ctx, part_w * 3, h, 'VIEW SCALE')
                    show_value(ctx, part_w * 3, h, '{:.3f}'.format(self.pads_pad_beat_scale))
 
            elif self.mode == self.MODE_EVENT:
                if self.event is not None and self.event.is_type_note():
                    # Slot 1, midi note
                    show_title(ctx, part_w * 0, h, 'NOTE')
                    show_value(ctx, part_w * 0, h, self.event.midinote)

                    # Slot 2, duration
                    show_title(ctx, part_w * 1, h, 'DURATION')
                    show_value(ctx, part_w * 1, h, '{:.3f}'.format(self.event.duration))

                    # Slot 3, micro time (utime)
                    show_title(ctx, part_w * 2, h, 'uTIME')
                    show_value(ctx, part_w * 2, h, '{:.3f}'.format(self.event.utime))

                    # Slot 4, chance
                    show_title(ctx, part_w * 3, h, 'CHANCE')
                    show_value(ctx, part_w * 3, h, "{:.0%}".format(self.event.chance))
                    
            elif self.mode == self.MODE_GENERATOR:
                show_title(ctx, part_w * 0, h, 'ALGORITHM')
                show_value(ctx, part_w * 0, h, self.generator_algorithm.name)

                for i, parameter in enumerate(self.generator_algorithm.get_algorithm_parameters()):
                    show_title(ctx, part_w * (i + 1), h, parameter['display_name'])
                    if parameter['type'] == float:
                        label = '{:.3f}'.format(parameter['value'])
                    else:
                        label = '{}'.format(parameter['value'])
                    show_value(ctx, part_w * (i + 1), h, label)

            # For all modes, slots 5-8 show clip preview
            if self.mode != self.MODE_GENERATOR or (self.mode == self.MODE_GENERATOR and len(self.generator_algorithm.get_algorithm_parameters()) <= 3):
                if self.clip.cliplengthinbeats > 0.0:
                    highglight_notes_beat_frame = (
                        self.pads_min_note_offset,
                        self.pads_min_note_offset + 8,
                        self.pads_pad_beats_offset,
                        self.pads_pad_beats_offset + 8 * self.pads_pad_beat_scale
                    )
                    draw_clip(ctx, self.clip, frame=(0.5, 0.0, 0.5, 0.87), highglight_notes_beat_frame=highglight_notes_beat_frame, event_color=track_color + '_darker1', highlight_color=track_color)
                
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
        if self.mode == self.MODE_CLIP:
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_2, definitions.BLACK)
            self.set_button_color_if_pressed(push2_python.constants.BUTTON_UPPER_ROW_3, animation=definitions.DEFAULT_ANIMATION)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_4, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_5, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_6, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_7, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_8, definitions.BLACK)

            self.push.buttons.set_button_color(push2_python.constants.BUTTON_CLIP, definitions.OFF_BTN_COLOR)

            self.set_button_color_if_pressed(push2_python.constants.BUTTON_DOUBLE_LOOP, animation=definitions.DEFAULT_ANIMATION)
            self.set_button_color_if_pressed(push2_python.constants.BUTTON_QUANTIZE, animation=definitions.DEFAULT_ANIMATION)
            self.set_button_color_if_pressed(push2_python.constants.BUTTON_DELETE, animation=definitions.DEFAULT_ANIMATION)

            if self.clip.recording or self.clip.willstartrecordingat > -1.0:
                if self.clip.recording:
                    self.push.buttons.set_button_color(push2_python.constants.BUTTON_RECORD, definitions.RED)
                else:
                    self.push.buttons.set_button_color(push2_python.constants.BUTTON_RECORD, definitions.RED, animation=definitions.DEFAULT_ANIMATION)
            else:
                self.push.buttons.set_button_color(push2_python.constants.BUTTON_RECORD, definitions.WHITE)

            track_color = self.app.track_selection_mode.get_track_color(self.clip.track.order)
            if self.clip.playing or self.clip.willplayat > -1.0:
                if self.clip.playing:
                    self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_1, track_color)
                else:
                    self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_1, track_color, animation=definitions.DEFAULT_ANIMATION)
            else:
                self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_1, track_color + '_darker1')
        
        elif self.mode == self.MODE_EVENT:
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_1, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_2, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_3, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_4, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_5, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_6, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_7, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_8, definitions.BLACK)

            self.push.buttons.set_button_color(push2_python.constants.BUTTON_DOUBLE_LOOP, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_QUANTIZE, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_DELETE, definitions.BLACK)
            
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_CLIP, definitions.BLACK)

        elif self.mode == self.MODE_GENERATOR:
            self.set_button_color_if_pressed(push2_python.constants.BUTTON_UPPER_ROW_1, animation=definitions.DEFAULT_ANIMATION) # generate sequence button
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_2, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_3, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_4, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_5, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_6, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_7, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_UPPER_ROW_8, definitions.BLACK)

            self.push.buttons.set_button_color(push2_python.constants.BUTTON_DOUBLE_LOOP, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_QUANTIZE, definitions.BLACK)
            self.push.buttons.set_button_color(push2_python.constants.BUTTON_DELETE, definitions.BLACK)

            self.push.buttons.set_button_color(push2_python.constants.BUTTON_CLIP, definitions.WHITE)
            
        if self.mode == self.MODE_CLIP or self.mode == self.MODE_EVENT:
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
        if self.mode == self.MODE_CLIP:
            if button_name == push2_python.constants.BUTTON_DOUBLE_LOOP:
                self.clip.double()
                return True
            elif button_name == push2_python.constants.BUTTON_QUANTIZE:
                self.quantize_helper()
                return True
            elif button_name == push2_python.constants.BUTTON_UPPER_ROW_3:
                self.quantize_helper()
                return True
            elif button_name == push2_python.constants.BUTTON_DELETE:
                self.clip.clear()
                return True
            elif button_name == push2_python.constants.BUTTON_UPPER_ROW_1:
                self.clip.play_stop()
                return True
            elif button_name == push2_python.constants.BUTTON_RECORD:
                self.clip.record_on_off()
                return True
            elif button_name == push2_python.constants.BUTTON_CLIP:
                self.mode = self.MODE_GENERATOR
                return True

        elif self.mode == self.MODE_GENERATOR:
            if button_name == push2_python.constants.BUTTON_UPPER_ROW_1:
                # Replace existing sequence with generated one
                self.set_new_generated_sequence()
                return True
            elif button_name == push2_python.constants.BUTTON_CLIP:
                # Go back to clip mode
                self.set_clip_mode(self.selected_clip_uuid)
                return True
        
        # For all modes
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
        
    def on_pad_pressed(self, pad_n, pad_ij, velocity, shift=False, select=False, long_press=False, double_press=False):
        notes_in_pad = self.notes_in_pad(pad_ij)
        if notes_in_pad:
            if not long_press:
                if self.mode != self.MODE_EVENT:
                    # Remove all notes
                    for note in notes_in_pad:
                        self.clip.remove_sequence_event(note.uuid)
                else:
                    # Exit event edit mode
                    self.set_clip_mode(self.selected_clip_uuid)
            else:
                if self.mode == self.MODE_EVENT:
                    self.set_clip_mode(self.selected_clip_uuid)
                # Enter event edit mode
                self.set_event_mode(notes_in_pad[0].uuid)
        else:
            if self.mode != self.MODE_EVENT:
                # Create a new note
                midi_note, timestamp = self.pad_ij_to_note_beat(pad_ij)
                self.clip.add_sequence_note_event(midi_note, velocity / 127, timestamp, self.pads_pad_beat_scale)
                if timestamp + self.pads_pad_beat_scale > self.clip.cliplengthinbeats:
                    # If adding a not beyond current clip length
                    self.clip.set_length(math.ceil(timestamp + self.pads_pad_beat_scale))
            else:
                # Exit event edit mode
                self.set_clip_mode(self.selected_clip_uuid)

        return True

    def on_encoder_rotated(self, encoder_name, increment):
        if self.mode == self.MODE_CLIP:
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
                        self.set_clip_mode(self.available_clips[next_clip_index])
                    else:
                        self.set_clip_mode(self.available_clips[0])
                return True  # Don't trigger this encoder moving in any other mode
                
            elif encoder_name == push2_python.constants.ENCODER_TRACK2_ENCODER:
                new_length = self.clip.cliplengthinbeats + increment
                if new_length < 1.0:
                    new_length = 1.0
                self.clip.set_length(new_length)
                return True  # Don't trigger this encoder moving in any other mode

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
                return True  # Don't trigger this encoder moving in any other mode
        
        elif self.mode == self.MODE_EVENT:
            if self.event is not None and self.event.is_type_note():
                if encoder_name == push2_python.constants.ENCODER_TRACK1_ENCODER:
                    self.event.set_midi_note(self.event.midinote + increment)
                    return True  # Don't trigger this encoder moving in any other mode
                elif encoder_name == push2_python.constants.ENCODER_TRACK2_ENCODER:
                    new_duration = round(100.0 * max(0.1, self.event.duration + increment/10))/100.0
                    self.event.set_duration(new_duration)
                    return True  # Don't trigger this encoder moving in any other mode
                elif encoder_name == push2_python.constants.ENCODER_TRACK3_ENCODER:
                    new_utime = self.event.utime + increment/1000.0
                    self.event.set_utime(new_utime)
                    return True  # Don't trigger this encoder moving in any other mode
                elif encoder_name == push2_python.constants.ENCODER_TRACK4_ENCODER:
                    new_chance = self.event.chance + 5 * increment/100.0
                    self.event.set_chance(clamp01(new_chance))
                    return True  # Don't trigger this encoder moving in any other mode

        elif self.mode == self.MODE_GENERATOR:
            if encoder_name == push2_python.constants.ENCODER_TRACK1_ENCODER:
                # Change selected generator algorithm
                current_algorithm_index = self.generator_algorithms.index(self.generator_algorithm)
                self.selected_generator_algorithm = (current_algorithm_index + 1) % len(self.generator_algorithms)
                return True  # Don't trigger this encoder moving in any other mode

            else:
                # Set algorithm parameter
                try:
                    encoder_index = [push2_python.constants.ENCODER_TRACK2_ENCODER,
                    push2_python.constants.ENCODER_TRACK3_ENCODER,
                    push2_python.constants.ENCODER_TRACK4_ENCODER,
                    push2_python.constants.ENCODER_TRACK5_ENCODER,
                    push2_python.constants.ENCODER_TRACK6_ENCODER,
                    push2_python.constants.ENCODER_TRACK7_ENCODER,
                    push2_python.constants.ENCODER_TRACK8_ENCODER].index(encoder_name)
                    try:
                        param = self.generator_algorithm.get_algorithm_parameters()[encoder_index]
                        self.generator_algorithm.update_parameter_value(param['name'], increment)
                    except IndexError:
                        pass
                except ValueError:
                    # Encoder not in list (not one of the parameter enconders)3
                    pass
                return True  # Don't trigger this encoder moving in any other mode

