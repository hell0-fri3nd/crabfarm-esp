import sqlite3
from datetime import datetime
from config import DB_FILE

def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    
    # Test records table
    c.execute('''CREATE TABLE IF NOT EXISTS test_records (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp DATETIME,
        do_value REAL,
        temperature REAL,
        compensated_do REAL,
        ph_value REAL,
        tds_value REAL,
        turbidity REAL,
        ammonium REAL,
        weight REAL,
        water_level_cm REAL,
        water_percent REAL
    )''')
    
    conn.commit()
    conn.close()
    print("Database initialized")

def save_test_record(data):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    timestamp = datetime.now().isoformat()
    
    c.execute('''INSERT INTO test_records 
        (timestamp, do_value, temperature, compensated_do, ph_value, 
         tds_value, turbidity, ammonium, weight, water_level_cm, water_percent)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
        (timestamp, data.get('do', 0), data.get('temperature', 0),
         data.get('compensated_do', 0), data.get('ph', 0),
         data.get('tds', 0), data.get('turbidity', 0),
         data.get('ammonium', 0), data.get('weight', 0),
         data.get('water_level', 0), data.get('water_percent', 0)))
    
    conn.commit()
    conn.close()

def get_last_5_records():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''SELECT timestamp, do_value, temperature, ph_value, 
                        tds_value, turbidity, ammonium, weight, water_level_cm
                 FROM test_records ORDER BY timestamp DESC LIMIT 5''')
    rows = c.fetchall()
    conn.close()
    
    records = []
    for row in rows:
        records.append({
            'timestamp': row[0],
            'do': row[1],
            'temperature': row[2],
            'ph': row[3],
            'tds': row[4],
            'turbidity': row[5],
            'ammonium': row[6],
            'weight': row[7],
            'water_level': row[8]
        })
    return records