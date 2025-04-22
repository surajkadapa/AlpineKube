import socket
import json
import zmq
from flask import Flask, request, jsonify, render_template

NODE_REGISTRY_SOCKET = "/tmp/node_registry.sock"
NODE_CREATION_SOCKET = "/tmp/node_creation.sock"

app = Flask(__name__, template_folder="templates")  # Ensure 'templates' folder is correct

@app.route("/")
def home():
    return render_template("index.html")  # Serves the HTML file

def query_node_registry():
    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(NODE_REGISTRY_SOCKET)
        client.sendall(b"GET_NODES")

        response = client.recv(8192).decode()
        client.close()

        return json.loads(response)
    except Exception as e:
        print(f"[ERR] Failed to connect to node registry: {e}")
        return []

@app.route('/nodes', methods=['GET'])
def get_nodes():
    return jsonify(query_node_registry())

@app.route('/create-node', methods=['POST'])
def create_node():
    data = request.json
    cpus = data.get("cpus")
    memory = data.get("memory")

    if not cpus or not memory:
        return jsonify({"error": "Missing CPU or memory values"}), 400

    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(NODE_CREATION_SOCKET)

        node_request = json.dumps({"cpus": cpus, "memory": memory}).encode()
        client.sendall(node_request)

        # Receive confirmation
        response = client.recv(1024).decode()
        client.close()

        return jsonify({"message": response})
    except Exception as e:
        print(f"[ERR] Node creation request failed: {e}")
        return jsonify({"error": "Failed to send node creation request"}), 500

@app.route('/upload-pod', methods=['POST'])
def upload_pod():
    pod_file = request.files.get('pod_file')
    zip_file = request.files.get('zip_file')

    if not pod_file or not zip_file:
        return jsonify({"error": "Both .pod and .zip files are required"}), 400

    try:
        pod_data = pod_file.read()
        zip_data = zip_file.read()

        context = zmq.Context()
        socket = context.socket(zmq.REQ)
        socket.connect("ipc:///tmp/pod_dispatcher.sock")

        socket.send_json({
            "pod_filename": pod_file.filename,
            "zip_filename": zip_file.filename
        }, zmq.SNDMORE)

        socket.send(pod_data, zmq.SNDMORE)

        socket.send(zip_data)

        result = socket.recv()
        return jsonify({"message": result.decode()})
    except Exception as e:
        print(f"[ERROR] {e}")
        return jsonify({"error": "Internal server error"}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
