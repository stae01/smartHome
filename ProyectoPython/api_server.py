from flask import Flask, request, jsonify
from flask_cors import CORS
import mysql.connector
from datetime import datetime
import logging

app = Flask(__name__)
CORS(app)
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

db_config = {
    'host':     '192.168.1.71',
    'user':     'esp32_user',
    'password': 'esp32_user',
    'database': 'smartHome',
    'port':     3306
}

def get_db_connection():
    conn = mysql.connector.connect(**db_config)
    logger.info("MySQL conectado")
    return conn

@app.route('/api/eventos', methods=['POST'])
def handle_event():
    data = request.json or {}
    print("📦 Datos recibidos:", data)

    required = ['humedad','gas','movimiento','puerta','alerta','modo_alarma','motivo']
    if not all(k in data for k in required):
        return jsonify({'error':'Datos incompletos'}), 400

    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        # Evento
        sql1 = """
          INSERT INTO Evento
            (fecha_hora, humedad, gas, movimiento, puerta, alerta)
          VALUES (%s, %s, %s, %s, %s, %s)
        """
        vals1 = (
          datetime.now(),
          data['humedad'],
          data['gas'],
          data['movimiento'],
          data['puerta'],
          data['alerta']
        )
        cursor.execute(sql1, vals1)
        event_id = cursor.lastrowid

        # Detalle
        sql2 = """
          INSERT INTO Evento_Detalle (id_evento, modo, motivo)
          VALUES (%s, %s, %s)
        """
        cursor.execute(sql2, (
          event_id,
          data['modo_alarma'],
          data['motivo']
        ))

        conn.commit()
        return jsonify({'success':True,'event_id':event_id}), 201

    except Exception as e:
        conn.rollback()
        logger.error(f"Error BD: {e}")
        return jsonify({'error':'Error interno'}), 500

    finally:
        cursor.close()
        conn.close()

if __name__=='__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
