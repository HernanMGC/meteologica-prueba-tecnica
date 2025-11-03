-- create the databases
CREATE TABLE IF NOT EXISTS weather (
    id INT AUTO_INCREMENT PRIMARY KEY, 
    date DATE NOT NULL,
    city VARCHAR(255) NOT NULL,
    temp_max FLOAT(10, 5) NOT NULL,
    temp_min FLOAT(10, 5) NOT NULL,
    precipitation FLOAT(10, 5) NOT NULL,
    cloudiness FLOAT(10, 5) NOT NULL
) ENGINE=INNODB;