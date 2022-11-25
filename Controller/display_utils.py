import cairo
import definitions
import push2_python

from utils import clamp, clamp01


def show_title(ctx, x, h, text, color=[1, 1, 1]):
    text = str(text)
    ctx.set_source_rgb(*color)
    ctx.select_font_face("Arial", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    font_size = h//12
    ctx.set_font_size(font_size)
    ctx.move_to(x + 3, 20)
    ctx.show_text(text)


def show_value(ctx, x, h, text, color=[1, 1, 1]):
    text = str(text)
    ctx.set_source_rgb(*color)
    ctx.select_font_face("Arial", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    font_size = h//8
    ctx.set_font_size(font_size)
    ctx.move_to(x + 3, 45)
    ctx.show_text(text)


def draw_text_at(ctx, x, y, text, font_size = 12, color=[1, 1, 1]):
    text = str(text)
    ctx.set_source_rgb(*color)
    ctx.select_font_face("Arial", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    ctx.set_font_size(font_size)
    ctx.move_to(x, y)
    ctx.show_text(text)


def show_rectangle(ctx,  x, y, width, height, background_color=None, alpha=1.0):
    display_w = push2_python.constants.DISPLAY_LINE_PIXELS
    display_h = push2_python.constants.DISPLAY_N_LINES
    ctx.save()
    if background_color is not None:
        ctx.set_source_rgba(*(definitions.get_color_rgb_float(background_color) + [alpha]))
    ctx.rectangle(x * display_w, y * display_h, width * display_w, height * display_h)
    ctx.fill()
    ctx.restore()


def show_text(ctx, x_part, pixels_from_top, text, height=20, font_color=definitions.WHITE, background_color=None, margin_left=4, margin_top=4, font_size_percentage=0.8, center_vertically=True, center_horizontally=False, rectangle_padding=0, rectangle_width_percentage=1.0):
    assert 0 <= x_part < 8
    assert type(x_part) == int

    display_w = push2_python.constants.DISPLAY_LINE_PIXELS
    display_h = push2_python.constants.DISPLAY_N_LINES
    part_w = display_w // 8
    x1 = part_w * x_part
    y1 = pixels_from_top

    ctx.save()

    if background_color is not None:
        ctx.set_source_rgb(*definitions.get_color_rgb_float(background_color))
        ctx.rectangle(x1 + rectangle_padding, y1 + rectangle_padding, rectangle_width_percentage * (part_w - rectangle_padding * 2), height - rectangle_padding * 2)
        ctx.fill()
    ctx.set_source_rgb(*definitions.get_color_rgb_float(font_color))
    ctx.select_font_face("Arial", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    font_size = round(int(height * font_size_percentage))
    text_lines = text.split('\n')
    n_lines = len(text_lines)
    if center_vertically:
        margin_top = (height - font_size * n_lines) // 2
    ctx.set_font_size(font_size)
    for i, line in enumerate(text_lines):
        if center_horizontally:
            (_, _, l_width, _, _, _) = ctx.text_extents(line)
            ctx.move_to(x1 + part_w/2 - l_width/2, y1 + font_size * (i + 1) + margin_top - 2)
        else:
            ctx.move_to(x1 + margin_left, y1 + font_size * (i + 1) + margin_top - 2)
        ctx.show_text(line)

    ctx.restore()

def show_notification(ctx, text, opacity=1.0):
    ctx.save()

    # Background
    display_w = push2_python.constants.DISPLAY_LINE_PIXELS
    display_h = push2_python.constants.DISPLAY_N_LINES
    initial_bg_opacity = 0.8
    ctx.set_source_rgba(0.0, 0.0, 0.0, initial_bg_opacity * opacity)
    ctx.rectangle(0, 0, display_w, display_h)
    ctx.fill()

    # Text
    initial_text_opacity = 1.0
    ctx.set_source_rgba(1.0, 1.0, 1.0, initial_text_opacity * opacity)
    font_size = display_h // 4
    ctx.set_font_size(font_size)
    margin_left = 8
    ctx.move_to(margin_left, 2.2 * font_size)
    ctx.show_text(text)

    ctx.restore()


def draw_clip(ctx, 
              clip,
              frame=(0.0, 0.0, 1.0, 1.0),  # (upper-left corner x, upper-left corner y, width, height)
              highglight_notes_beat_frame=None,  # (min note, max note, min beat, max_beat)
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
    y = display_h * (yoffset_percentage + height_percentage)
    width = display_w * width_percentage
    height = display_h * height_percentage

    if highglight_notes_beat_frame is not None:
        displaybeatslength = max(clip.cliplengthinbeats, highglight_notes_beat_frame[3])
    else:
        displaybeatslength = clip.cliplengthinbeats

    if background_color is not None:
        show_rectangle(ctx, xoffset_percentage, yoffset_percentage, width_percentage, height_percentage, background_color=background_color)
    
    rendered_notes = [event for event in clip.sequence_events if event.type == 1 and event.renderedstarttimestamp >= 0.0]
    all_midinotes = [int(note.midinote) for note in rendered_notes]
    playhead_position_percentage = clip.playheadpositioninbeats/displaybeatslength

    if len(all_midinotes) > 0:
        min_midinote = min(all_midinotes)
        max_midinote = max(all_midinotes) + 1  # Add 1 to highest note does not fall outside of screen
        note_height = height / (max_midinote - min_midinote)
        for note in rendered_notes:
            note_height_percentage =  (int(note.midinote) - min_midinote) / (max_midinote - min_midinote)
            note_start_percentage = float(note.renderedstarttimestamp) / displaybeatslength
            note_end_percentage = float(note.renderedendtimestamp) / displaybeatslength
            if note_start_percentage <= note_end_percentage:  
                # Note does not wrap across clip boundaries, draw 1 rectangle  
                if (note_start_percentage <= playhead_position_percentage <= note_end_percentage + 0.05) and clip.playheadpositioninbeats != 0.0: 
                    color = highlight_color
                else:
                    color = event_color
                x0_rel = (x + note_start_percentage * width) / display_w
                y0_rel = (y - (note_height_percentage * height + note_height)) / display_h
                width_rel = ((x + note_end_percentage * width) / display_w) - x0_rel
                height_rel = note_height / display_h
                show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)
            else:
                # Draw "2 rectangles", one from start of note to end of section, and one from start of section to end of note
                if (note_start_percentage <= playhead_position_percentage or (playhead_position_percentage <= note_end_percentage + 0.05 and note_end_percentage != 0.0)) and clip.playheadpositioninbeats != 0.0: 
                    color = highlight_color
                else:
                    color = event_color
                    
                x0_rel = (x + note_start_percentage * width) / display_w
                y0_rel = (y - (note_height_percentage * height + note_height)) / display_h
                width_rel = ((x + clip.cliplengthinbeats/displaybeatslength * width) / display_w) - x0_rel
                height_rel = note_height / display_h
                show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)

                x0_rel = (x + 0.0 * width) / display_w
                y0_rel = (y - (note_height_percentage * height + note_height)) / display_h
                width_rel = ((x + note_end_percentage * width) / display_w) - x0_rel
                height_rel = note_height / display_h
                show_rectangle(ctx, x0_rel, y0_rel, width_rel, height_rel, background_color=color)

        if highglight_notes_beat_frame is not None:
            y0 = y/display_h - (((highglight_notes_beat_frame[0] - min_midinote) * note_height))/display_h
            h = note_height / display_h * (highglight_notes_beat_frame[1] - highglight_notes_beat_frame[0])
            x0 = xoffset_percentage + highglight_notes_beat_frame[2]/displaybeatslength * width_percentage
            w = (highglight_notes_beat_frame[3] - highglight_notes_beat_frame[2])/displaybeatslength* width_percentage
            show_rectangle(ctx, x0, y0 - h, w, h, background_color=definitions.WHITE, alpha=0.25)
