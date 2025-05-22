CREATE USER 'esp32_user'@'%' IDENTIFIED BY 'esp32_user';
GRANT ALL PRIVILEGES ON smarthome.* TO 'esp32_user'@'%';
FLUSH PRIVILEGES;

SELECT User, Host FROM mysql.user WHERE User = 'esp32_user';

-- Crear base de datos y seleccionar
CREATE DATABASE IF NOT EXISTS smartHome;
USE smartHome;

-- Crear tabla Evento
CREATE TABLE Evento (
 id_evento INT AUTO_INCREMENT PRIMARY KEY,
 fecha_hora DATETIME NOT NULL,
 humedad FLOAT,
 gas INT,
 movimiento BOOLEAN,
 puerta BOOLEAN,
 alerta BOOLEAN
);
-- Crear tabla Sensor
CREATE TABLE Sensor (
 id_sensor INT AUTO_INCREMENT PRIMARY KEY,
 tipo TEXT NOT NULL,
 descripcion TEXT
);
-- Crear tabla intermedia Evento_Sensor (Relación N:M)
CREATE TABLE Evento_Sensor (
 id_evento INT,
 id_sensor INT,
 PRIMARY KEY (id_evento, id_sensor),
 FOREIGN KEY (id_evento) REFERENCES Evento(id_evento) ON DELETE CASCADE,
 FOREIGN KEY (id_sensor) REFERENCES Sensor(id_sensor) ON DELETE CASCADE);
 
 CREATE TABLE Evento_Detalle (
    id_detalle INT AUTO_INCREMENT PRIMARY KEY,
    id_evento INT NOT NULL,
    modo VARCHAR(50),
    motivo TEXT,
    FOREIGN KEY (id_evento) REFERENCES Evento(id_evento) ON DELETE CASCADE
);
