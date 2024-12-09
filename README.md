#first tab:

gcc -o out servers.c

./out

#new tab:

curl -X GET http://localhost:8081/index.html 

-> result in first tab:Response sent: 200 OK

curl -X GET http://localhost:8081/indexxx.html

-> result in first tab:Response sent: 404 Not Found
