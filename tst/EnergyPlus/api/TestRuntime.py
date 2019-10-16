import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'api'))

from energyplus_api import EnergyPlusAPI


api = EnergyPlusAPI()
runtime = api.runtime()


def hour_handler():
    print("OH HAI NEW HOUR")
    sys.stdout.flush()


runtime.register_callback_end_of_hour(hour_handler)
runtime.run_energyplus('/tmp/epdll'.encode('utf-8'))
