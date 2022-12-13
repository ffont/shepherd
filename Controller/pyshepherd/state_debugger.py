import asyncio
import logging
import threading

from flask import Flask, render_template


state_debugger_port = 5100
state_debugger_autoreload_ms = 1000
disable_flask_logging = True

state_debugger_server = Flask(__name__)
ss_instance = None

if disable_flask_logging:
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.ERROR)


@state_debugger_server.route('/')
def state_debugger():
    if ss_instance is None or ss_instance.state is None:
        state = 'No state has been synced yet'
    else:
        state = '{}'.format(ss_instance.state.render(include_attributes=True))
    return render_template('state_debugger.html',
                           state=state,
                           xml=False,
                           state_debugger_autoreload_ms=state_debugger_autoreload_ms)


class StateDebuggerServerThread(threading.Thread):

    def __init__(self, port, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.port = port

    def run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        print('* Starting state debugger in port http://localhost:{}'.format(self.port))
        state_debugger_server.run(host='0.0.0.0', port=self.port, debug=True, use_reloader=False)


def start_state_debugger(_ss_instance, port=5100):
    global ss_instance
    ss_instance = _ss_instance
    StateDebuggerServerThread(port).start()





