from pyshepherd.pyshepherd import ShepherdBackendControllerApp
import time


class App(ShepherdBackendControllerApp):

    def on_backend_connected(self):
        print('on_backend_connected')

    def on_backend_state_ready(self):
        print('on_backend_state_ready')

    def on_state_update_received(self, update_data):
        print(update_data)

    def __init__(self):
        super().__init__()
        while True:
            time.sleep(1)


app = App()
