-- create the databases
CREATE TABLE IF NOT EXISTS weather (
    date DATE NOT NULL,
    city VARCHAR(255) NOT NULL,
    temp_max FLOAT(10, 5),
    temp_min FLOAT(10, 5),
    precipitation FLOAT(10, 5),
    cloudiness FLOAT(10, 5),
    CONSTRAINT PK_WeatherLine PRIMARY KEY (date, city)
) ENGINE=INNODB;