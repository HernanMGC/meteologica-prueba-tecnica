-- create the databases
CREATE TABLE IF NOT EXISTS weather (
    id INT AUTO_INCREMENT PRIMARY KEY, 
    date DATE NOT NULL
--     , city TEXT NOT NULL
--     , temp_max DECIMAL(10, 5) NOT NULL
--     , temp_min DECIMAL(10, 5) NOT NULL
--     , precipitation DECIMAL(10, 5) NOT NULL
--     , cloudiness DECIMAL(10, 5) NOT NULL
) ENGINE=INNODB;