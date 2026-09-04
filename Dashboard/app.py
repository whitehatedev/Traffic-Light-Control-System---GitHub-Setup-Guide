from flask import Flask, render_template, request, jsonify
from flask_cors import CORS
import requests
from datetime import datetime

app = Flask(__name__)
app.secret_key = 'traffic_control_secret_2024'
CORS(app)

# ===================== DEVICE CONFIGURATION =====================
DEVICE_CONFIG = {
    1: {
        'name': 'ESP32-1',
        'location': '📍 Intersection A',
        'ip': '192.168.1.100',
        'capabilities': {
            'F01': True, 'F02': True, 'F03': True,
            'F04': True, 'F05': True, 'F06': True
        },
        'has_sensors': False
    },
    2: {
        'name': 'ESP32-2',
        'location': '📍 Intersection B',
        'ip': '192.168.1.101',
        'capabilities': {
            'F01': True, 'F02': True, 'F03': True,
            'F04': True, 'F05': True, 'F06': True
        },
        'has_sensors': True
    }
}

FAULT_NAMES = ['LED Burnout', 'Overheating', 'Water Leakage', 'Phase Skipping', 'Over-Timing', 'Under-Timing']
FAULT_CODES = ['F01', 'F02', 'F03', 'F04', 'F05', 'F06']

system_state = {
    'device1': {'connected': False, 'state': None, 'history': []},
    'device2': {'connected': False, 'state': None, 'history': []}
}

@app.route('/')
def index():
    return render_template('fault_creation.html',
                           devices=DEVICE_CONFIG,
                           fault_names=FAULT_NAMES,
                           fault_codes=FAULT_CODES)

@app.route('/creation')
def fault_creation():
    return render_template('fault_creation.html',
                           devices=DEVICE_CONFIG,
                           fault_names=FAULT_NAMES,
                           fault_codes=FAULT_CODES)

@app.route('/resolution')
def fault_resolution():
    return render_template('fault_resolution.html',
                           devices=DEVICE_CONFIG,
                           fault_names=FAULT_NAMES,
                           fault_codes=FAULT_CODES)

@app.route('/api/connect', methods=['POST'])
def connect_device():
    data = request.json
    device_id = data.get('device_id', 1)
    ip = data.get('ip', DEVICE_CONFIG[device_id]['ip'])

    DEVICE_CONFIG[device_id]['ip'] = ip

    try:
        response = requests.get(f'http://{ip}/state', timeout=3)
        if response.status_code == 200:
            state_key = f'device{device_id}'
            system_state[state_key]['connected'] = True
            system_state[state_key]['state'] = response.json()

            try:
                requests.post(f'http://{ip}/connect', json={'connected': True}, timeout=1)
            except:
                pass

            return jsonify({
                'success': True,
                'message': f'Connected to {DEVICE_CONFIG[device_id]["name"]}',
                'device': device_id,
                'state': system_state[state_key]['state']
            })
        else:
            return jsonify({'success': False, 'message': 'Device returned error'})
    except requests.exceptions.RequestException as e:
        return jsonify({'success': False, 'message': f'Connection failed: {str(e)}'})

@app.route('/api/state/<int:device_id>', methods=['GET'])
def get_state(device_id):
    ip = DEVICE_CONFIG[device_id]['ip']
    try:
        response = requests.get(f'http://{ip}/state', timeout=2)
        if response.status_code == 200:
            esp_data = response.json()
            state_key = f'device{device_id}'
            system_state[state_key]['state'] = esp_data
            return jsonify(esp_data)
        else:
            return jsonify({'error': 'Device error'}), 500
    except requests.exceptions.RequestException as e:
        return jsonify({'error': f'Device unreachable: {str(e)}'}), 500

@app.route('/api/command/<int:device_id>', methods=['POST'])
def send_command(device_id):
    data = request.json
    endpoint = data.get('endpoint')
    payload = data.get('payload', {})

    ip = DEVICE_CONFIG[device_id]['ip']
    url = f'http://{ip}/{endpoint}'

    try:
        response = requests.post(url, json=payload, timeout=2)
        if response.status_code == 200:
            result = response.json()
            state_key = f'device{device_id}'
            system_state[state_key]['history'].append({
                'timestamp': datetime.now().strftime('%H:%M:%S'),
                'action': data.get('action', 'Command'),
                'message': result.get('message', '')
            })
            return jsonify(result)
        else:
            return jsonify({'success': False, 'message': f'Error: {response.status_code}'}), 500
    except requests.exceptions.RequestException as e:
        return jsonify({'success': False, 'message': f'Request failed: {str(e)}'}), 500

@app.route('/api/fault/inject', methods=['POST'])
def inject_fault():
    data = request.json
    device_id = data.get('device_id', 1)
    fault_type = data.get('fault_type')
    road = data.get('road', 0)
    led_index = data.get('led_index', None)
    seconds = data.get('seconds', None)

    ip = DEVICE_CONFIG[device_id]['ip']
    caps = DEVICE_CONFIG[device_id]['capabilities']

    fault_map = {'burn': 'F01', 'skip': 'F04', 'over': 'F05', 'under': 'F06'}
    fault_code = fault_map.get(fault_type)

    if not fault_code or not caps.get(fault_code, False):
        return jsonify({'success': False, 'message': f'Fault {fault_code} not supported'})

    try:
        if fault_type == 'burn':
            leds = [False, False, False]
            if led_index is not None and 0 <= led_index < 3:
                leds[led_index] = True
            else:
                leds = [True, True, True]
            payload = {'fault': 0, 'road': road, 'leds': leds}
            response = requests.post(f'http://{ip}/fault/add', json=payload, timeout=2)

        elif fault_type == 'skip':
            payload = {'fault': 3, 'road': road}
            response = requests.post(f'http://{ip}/fault/add', json=payload, timeout=2)
            if response.status_code == 200:
                next_road = (road + 1) % 4
                response = requests.post(f'http://{ip}/advance', json={'targetRoad': next_road}, timeout=2)

        elif fault_type == 'over':
            payload = {'fault': 4, 'road': road}
            if seconds is not None:
                payload['seconds'] = seconds
            response = requests.post(f'http://{ip}/fault/add', json=payload, timeout=2)

        elif fault_type == 'under':
            payload = {'fault': 5, 'road': road}
            if seconds is not None:
                payload['seconds'] = seconds
            response = requests.post(f'http://{ip}/fault/add', json=payload, timeout=2)
        else:
            return jsonify({'success': False, 'message': 'Unknown fault type'})

        if response.status_code == 200:
            result = response.json()
            state_key = f'device{device_id}'
            system_state[state_key]['history'].append({
                'timestamp': datetime.now().strftime('%H:%M:%S'),
                'action': f'Inject {fault_type}',
                'message': result.get('message', 'Success')
            })
            return jsonify(result)
        else:
            return jsonify({'success': False, 'message': f'Error: {response.status_code}'})

    except requests.exceptions.RequestException as e:
        return jsonify({'success': False, 'message': f'Request failed: {str(e)}'})

@app.route('/api/fault/resolve', methods=['POST'])
def resolve_fault():
    data = request.json
    device_id = data.get('device_id', 1)
    fault_code = data.get('fault_code')
    road = data.get('road', 0)
    led_index = data.get('led_index', None)

    ip = DEVICE_CONFIG[device_id]['ip']
    caps = DEVICE_CONFIG[device_id]['capabilities']

    fault_key = FAULT_CODES[fault_code]
    if not caps.get(fault_key, False):
        return jsonify({'success': False, 'message': f'Fault {fault_key} not supported'})

    try:
        if fault_code == 0:
            leds = [False, False, False]
            if led_index is not None and 0 <= led_index < 3:
                leds[led_index] = False
            payload = {'fault': 0, 'road': road, 'leds': leds}
            response = requests.post(f'http://{ip}/fault/remove', json=payload, timeout=2)
        else:
            payload = {'fault': fault_code, 'road': road}
            response = requests.post(f'http://{ip}/fault/remove', json=payload, timeout=2)

        if response.status_code == 200:
            result = response.json()
            state_key = f'device{device_id}'
            system_state[state_key]['history'].append({
                'timestamp': datetime.now().strftime('%H:%M:%S'),
                'action': f'Resolve {FAULT_CODES[fault_code]}',
                'message': result.get('message', 'Success')
            })
            return jsonify(result)
        else:
            return jsonify({'success': False, 'message': f'Error: {response.status_code}'})

    except requests.exceptions.RequestException as e:
        return jsonify({'success': False, 'message': f'Request failed: {str(e)}'})

@app.route('/api/history/<int:device_id>', methods=['GET'])
def get_history(device_id):
    state_key = f'device{device_id}'
    return jsonify(system_state[state_key]['history'][-50:])

if __name__ == '__main__':
    print("\n" + "=" * 60)
    print("🚦 Traffic Light Control System - Dual Dashboards")
    print("=" * 60)
    print("📌 Fault Creation Dashboard:   http://localhost:5000/creation")
    print("📌 Fault Resolution Dashboard: http://localhost:5000/resolution")
    print("=" * 60 + "\n")
    app.run(host='0.0.0.0', port=5000, debug=True, threaded=True)
