Prueba técnica Meteológica
==========================

**Arquitectura**

La prueba técnica cuenta con los siguientes servicios:

* mysql: Una imagen MySQL sirviendo una base de datos.
* phpmyadmin: Una imagen phpmyadmin como herramienta de desarrollo para visualizar el estado de la base de datos
* service-a: Una imagen linux con un micro servidor basado en Crow para el Service A de la prueba.
* service-b: Una imagen linux con un micro servidor basado en Crow para el Servicio B de la prueba.
* cache: Una imagen de Redis para servir una caché para las consultas del Service B hacia el Service A.

Los servicios están orquestados por Docker Compose y se han establecido dependencias entre ellos de modo que tanto phpmyadmin y service-a dependan de mysql, y el service-b dependa de cache. De esta forma, esperarán hasta que mysql esté inicializado y haya pasado su healthcheck para inicializarse ellos.

Así mismo, service-a y service-b tienen sendos healthcheck que los reinician si no devuelven un estado satisfactorio.

*MySQL*
El servicio mysql cuenta con un script de inicialización que se lanzará la primera vez que se levante Docker para inicializar la base datos con las tablas necesarias para el funcionamiento de los servicios. La persistencia de la Base de Datos se mantiene mediante el volumen mysql-data.

*Service A y Service B*

Se ha tomado como esqueleto para ambos servicios la imagen del proyecto Todo List RESTful API in C++ with Docker, puesto que es un ejemplo de microservicio en C++ asequible sobre el que construir este test. Aunque se ha adaptado para las necesidad del proyecto, queda pendiente para futuro optimizar en mayor profundidad la imagen para lo estrictamente necesario para este ejercicio.

**Lanzamiento de los servicios**

Para lanzar los servicios el primer paso es descomprimir el .zip adjunto o clonar el repositorio en el que se encuentra el proyecto.

```
$ git clone https://github.com/HernanMGC/meteologica-prueba-tecnica
```

Una vez se ha descomprimido el fichero o ha acabado el clonado del repositorio habrá que acceder a la carpeta generada y ejecutar el siguiente comando.

```
$ docker compose up -d
```

Cuando el proceso termine los cuatro servicios estarán levantados y funcionando.

**Probar los servicios**

Dentro del directorio raíz del proyecto se puede encontrar un directorio para cada servicio service-a y service-b, en el interior de cada una se pueden encontrar sendas especificaciones OpenAPI openapi.yaml con la documentación de qué endpoints tiene disponibles y cómo funcionan cada uno de los servicios.

Adicionalmente se ha preparado una colección Postman para poder probar los servicios con el siguiente enlace de invitación. No obstante, se acompaña el proyecto con un conjunto de scripts shell script y bat con las llamadas cURL pertinentes.

* service-a-health.*: Consulta si el Service A está activo y funcionando.
* service-a-cities.*: Consulta al Service A por todas las ciudades registradas en la DB.service-a-ingest.*: Hace una ingesta en el Service A del fichero meteo.csv incluido en el directorio del proyecto.
* service-a-weather.*: Consulta al Service A sobre la primera página de las entradas registradas para Madrid del 2025-11-01 al 2025-11-1 limitando los resultados a 16.
* service-b-health.*: Consulta si el Servicio B está activo y funcionando.
* service-b-weather-daily.*: Consulta agregada por días al Servicio B de los días 10 consecutivos desde el día 2025-11-01 en grados centígrados sobre temperaturas máximas, mínimas y medias de cada día, así como sobre precipitaciones y nubosidad.
* service-b-weather-rolling7.*: Consulta agregada por días al Servicio B de los días 10 consecutivos desde el día 2025-11-01 en grados centígrados sobre las medias móviles de temperatura, precipitaciones y nubosidad.

**Desarrollo de los servicios**
*Consideraciones generales*

Los servicios han sido desarrollados asumiendo que la base de datos devolverá todas los datos con las fechas consecutivas. Esto tiene margen de mejora y plantea la adición de mecanismos de comprobación para comprobar que cada una de las entradas devueltas por la base datos corresponden a fechas consecutivas y que no falta datos ninguna fecha del intervalo, así como que su orden es el correcto.

*MySQL*

El esquema de la base de datos es el siguiente:

date DATE NOT null
city VARCHAR(255) NOT null
temp_max FLOAT(10, 5)
temp_min FLOAT(10, 5)
precipitation FLOAT(10, 5
cloudiness FLOAT(10, 5)
Y la clave primaria es la dupla date y city.

*Service A*

La normalización y validación de datos a la hora de ingestar el CSV en la base de datos ha tenido como criterio principal maximizar la información recabada por lo que se ha intentado asegurar la mayor cantidad de inserciones dada la muestra de entradas del fichero meteo.csv adjunto con la prueba.

Se han aplicado funciones de trim a las cadenas de ciudades y fechas. Las fechas se han transformado del formato YYYY/MM/DD al formato YYYY-MM-DD. Cualquier formato distinto a YYYY/MM/DD del fichero ingestado será descartado como un error. Una posible mejora para este proceso para asegurar más inserciones sería admitir más formatos, como el formato objetivo y otros que no cause problemas de ambigüedad entre MM y DD.

Otra posible mejora respecto a las ciudades sería hacer comprobaciones de casing y caracteres especiales para evitar que entradas relativas a mismas ciudades puedan interpretarse como entradas de ciudades distintas.

Todos los valores numéricos son flotantes introducidos por texto, por lo que a la hora de parsearlos se ha aplicado una regla de sustitución por la que se reemplazan las , (comas) por . (puntos) para ajustarse a las reglas de la base de datos y JSON. En los casos en los que el valor separado por comas sea vacío se introducirán en la base de datos el valor null.

*Service B*

Los valores null en los cálculos agregados anularán, es decir, harán que se devuelva null a aquellos valores de respuesta que dependan de su valor. Así una temperatura media cuyo máximo o mínimo sean null valdrá null. De la misma manera en la respuesta agregada rolling7 para la llamada weather/{city} del Servicio B devolverá null en lugar de los datos agregado de un día si no hay valores suficientes para hacer las medias móviles de 7 días.

Para las consultas de weather/{city} con agg=rolling7 se está haciendo una consulta por día de la ventana, es decir, siete consultas. Esto facilita el tratamiento de los datos y la comprobación de tener el conjunto completo de días consecutivos necesario para hacer los cálculos, permitiendo descartar aquella información que no vaya a ser veraz

En el futuro podría plantearse, además de la caché de consultas, reducir las queries de siete a uno determinando cuáles son la fecha inicio del primer día de la primera ventana y la fecha de final de la última ventana, paro luego establecer reglas de comprobación que aseguren que hay suficientes fechas consecutivas con datos para poder hacer los cálculos  de cada media móvil.

Cada consulta que Service B hace a Service A se está cacheando tomando como key el hash SHA256 de la cadena de la consulta a Service A y como valor la respuesta de ese servicio. El TTL se establece en 10 minutos para cada key.

**Reflexión**

Ha sido interesante volver a enfrentarme a esta clase de desafíos usando Docker y creando servicios web después de varios años enfocado al desarrollo de videojuegos. Desde ya muchas gracias por permitirme la oportunidad de hacer la prueba y desempolvar estas herramientas. Ha sido un gran ejercicio de reaprendizaje de todo lo cubierto en el desarrollo de la prueba, desde Docker a Base de Datos, pasando por APIs y gestiones de sistema.
Con más tiempo me encantaría limpiar más el código y expandir lo desarrollado, pero gran parte de la primera semana desde el miércoles siguiente a que me la mandarais fue dedicada a analizar el problema y a hacer pruebas pequeñas con Docker para recordar y aprender lo necesario.
