from pyshepherd.pyshepherd import ShepherdBackendInterface


class ShepherdInterface(object):

    app = None

    state_transport_check_thread = None
    state_tracks_check_thread = None

    last_received_tracks_raw_state = ""
    parsed_state = {}

    showing_countin_message = False

    def __init__(self, app):
        self.app = app
        self.sbi = ShepherdBackendInterface(app, ws_port=8126, verbose=False, debugger_port=5100)
        self.sbi.send_msg_to_app('/shepherdControllerReady', [])

    @property
    def session(self):
        # Will return None if no session state is loaded
        if self.sbi.state is None:
            return None
        return self.sbi.state.session

    @property
    def state(self):
        # Will return None if no state is loaded
        return self.sbi.state

    def reactivate_modes(self):
        self.app.active_modes_need_reactivate = True

    def receive_shepherd_ready(self):
        self.reactivate_modes()
        self.app.midi_cc_mode.initialize()
        #self.app.notes_midi_in = None # This used to be here in previous controller implementation, not sure if it is needed
