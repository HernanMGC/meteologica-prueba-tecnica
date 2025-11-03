-- create the databases
CREATE TABLE IF NOT EXISTS weather (
    id INT AUTO_INCREMENT NOT NULL, 
    date DATE NOT NULL,
    city VARCHAR(255) NOT NULL,
    temp_max FLOAT(10, 5) NOT NULL,
    temp_min FLOAT(10, 5) NOT NULL,
    precipitation FLOAT(10, 5) NOT NULL,
    cloudiness FLOAT(10, 5) NOT NULL,
    CONSTRAINT PK_WeatherLine PRIMARY KEY (id, date, city)
) ENGINE=INNODB;