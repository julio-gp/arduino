//nodeMCU v1.0

#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>     //Memória Interna
#include <Wire.h>       //I2C
#include <LCD.h>
#include <LiquidCrystal_I2C.h> //I2C

// Def
#define myPeriodic 2 //in sec | Thingspeak pub is 15sec
#define ONE_WIRE_BUS 2  // D4 (DS18B20)
#define led           14 //D5
#define RES_frio      13 //D7 (led vermelho)
#define RES_quente    12 //D6 (led azul)
#define botao_menos   16 //D0
#define botao_mais    0  //D3

LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7,3, POSITIVE);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// Define servidor na porta 80
WiFiServer server(80);

// Rede local
const char* rede = "didil 2"; 
const char* senha = "eapdidil";
// IP: http://192.168.1.100


// Mapeamento da EEPROM  
// 0 - temperatura alvo - parte inteira (var temp_alvo)
// 1 - temperatura alvo - parte decimal (var temp_alvo)
// 2 - intervalo thingspeak - parte inteira
// 3 - intervalo thingspeak - parte decimal
// 4 - intervalo de medição - parte inteira
// 5 - intervalo de medição - parte decimal
// 6 - tolerancia - parte inteira
// 7 - tolerancia - parte decimal
// 8 - temperatura alvo - sinal (0 - positivo | 1 - negativo)
// 9 - modo cervejeira ou brewery (var cervejeira)
// 12 - intervalo de proteção do freezer (em minutos)  (NÃO FUNCIONOU)
// *** obs.: Cada alocação tem vida útil de 10.000 escritas. Qdo estragar tem que trocar pra outro número.



// Variáveis
int count;
short atividade; //0: inativo  -  1: refrigerador  -  -1: aquecedor
unsigned short modo; //0: leitura de temp  -  1: ajuste de temp_alvo

unsigned long inicio;
unsigned long intervalo_inicial;

unsigned short tempo_botao;
unsigned long ultimo_tempo;
unsigned long ultima_medicao;
unsigned long ultimo_envio;
unsigned long ultimo_desligamento;
unsigned int intervalo_TS;
unsigned int intervalo_medicao;
unsigned int intervalo_protecao_freezer;

unsigned long tempo_programado;
unsigned long tempo_programado_final;
float temperatura_programada;
boolean alvo_programado;
boolean valendo;
String alias_programado;
String cor_fundo_1;
String cor_fundo_2;
String status_cervejeira;

float prevTemp = 0;
boolean solta_botao;
boolean Retroluz;     // 0: Retroluz desligada     1: Retroluz ligada
boolean reading = false;
float temp;
float temp_padrao_cervejeira;
float dt;
String no_atividade;
String alias_atividade;
String status_wifi = "!!";
// Parâmetros
float temp_alvo;
float tolerancia;
unsigned short intervalo;
boolean cervejeira;

void setup() {
  Serial.begin(115200);
  lcd.begin (16,2);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("     CALEJU    ");
  modo = 0;
  valendo = false;
  //intervalo_TS = 15000;
  //intervalo_medicao = 5000;
  intervalo_inicial = 15000; // 15 segundos
  solta_botao = 0;
  temp_padrao_cervejeira = -4.0;
  cor_fundo_1 = "#F0F8FF";
  cor_fundo_2 = "#F0F8FF";
  alvo_programado = 0;
  tempo_programado = 0;
  temperatura_programada = 0;
  tempo_botao = 2000;
  atividade = 0;
  Retroluz = 1;
  EEPROM.begin(4096);
  //EEPROM.write(6, 0);
  //EEPROM.write(7, 5); 
  if (EEPROM.read(9) > 0) {cervejeira = 1;}
  else {cervejeira = 0;}
  temp_alvo = EEPROM.read(0) + float(EEPROM.read(1))/10;
  if (EEPROM.read(8) != 0) {temp_alvo = temp_alvo * -1;}
  tolerancia = EEPROM.read(6) + float(EEPROM.read(7))/10;
  intervalo_TS = (int(EEPROM.read(2)) * 60 + int(EEPROM.read(3))) * 1000; //15000
  intervalo_medicao = (int(EEPROM.read(4)) * 60 + int(EEPROM.read(5))) * 1000; //5000
  intervalo_protecao_freezer = EEPROM.read(12)*60*1000; 20*60*1000;  //20 minutos      //
  EEPROM.end();
  pinMode(led, OUTPUT);
  pinMode(RES_frio, OUTPUT);
  pinMode(RES_quente, OUTPUT);
  digitalWrite(RES_frio, HIGH);
  digitalWrite(RES_quente, HIGH);  
  pinMode(botao_mais, INPUT_PULLUP);
  pinMode(botao_menos, INPUT_PULLUP);
  lcd.setCursor(0,1);
  lcd.print("Conectando...      ");
  conectaWifi();
  server.begin();
  Serial.print("Servidor iniciado - IP: ");
  Serial.println(WiFi.localIP());
  /*if (WiFi.localIP() == "0.0.0.0") {
    lcd.setCursor(0,1);
    lcd.print(String(WiFi.localIP()) + "NAO CONECTADO!!!            ");
    delay(2000);
  }*/
  ultimo_envio = millis();
  ultima_medicao = millis();
  inicio = millis();
  ultimo_desligamento = 0;
  
}

void loop() 
{
  //Serial.println("EEPROM no início do loop: " + String(EEPROM.read(12)));
  
  // Tempo de espera inicial para não tomar qualquer medida até que o termômetro se ajuste.
  if (valendo == false) {
    if ((millis() - inicio) > intervalo_inicial) {valendo = true;}
  }

// MODO 0 - LEITURAS ---------------------------------------
  if (modo==0) {

    delay(1000);
    reading = 0;
    
    if (WiFi.status() == 3) {status_wifi = "ok";}
    else {status_wifi = "!!";}
    
    if (cervejeira == 1) {status_cervejeira = "C";}
    else {status_cervejeira = "B";}

    //Serial.print(WiFi.status());
    //Serial.print(server.status());
    //if (alvo_programado == 1) {Serial.println(tempo_programado_final - millis());} else {Serial.print("NP.");}

    // proteção para temperaturas muito baixas 18/04/2018 (depois de problemas com energia a temperatura alvo foi para -25)
    if (cervejeira == 1 and temp_alvo < temp_padrao_cervejeira) {temp_alvo = temp_padrao_cervejeira;}
    else if (cervejeira == 0 and temp_alvo < -0.5) {temp_alvo = -0.5;}

    // quando atinge o tempo programado
    if ( (alvo_programado == 1) and (millis() >= tempo_programado_final) )
    {
      Serial.println("temperatura alvo modificada");
      temp_alvo = temperatura_programada;
      EEPROM.begin(4096);     
      EEPROM.write(0, abs(temp_alvo));
      EEPROM.write(1, (abs(temp_alvo) - int(abs(temp_alvo))) * 10);
      if (temp_alvo >= 0) {EEPROM.write(8, 0);}
      else {EEPROM.write(8, 1);}     
      EEPROM.end();       
      cor_fundo_2="#F0F8FF";
      alvo_programado = 0;
      temperatura_programada = 0;
      tempo_programado = 0;
      tempo_programado_final = 0;
      alias_programado = "";
    }
    
    // botão menos pressionado - Vai pra configuracao (modo 1)
    if (digitalRead(botao_menos) == LOW) {
       modo = 1;
       solta_botao = 1;
       return;
     } 
    
    // botão mais pressionado - Liga e desliga luz do display  |  Se apertar mais tempo o botão menos, liga modo cervejeira    
    if (digitalRead(botao_mais) == LOW) {
      ultimo_tempo = millis();
      while (digitalRead(botao_mais) == LOW) {
        delay(100);
        if ((millis() - ultimo_tempo) > 2*tempo_botao) { alternaCervejeiraBrewery(); }
      }
      Serial.println("botao mais");
      if (Retroluz == 1 and reading == 0) {
        lcd.setBacklight(LOW);
        Retroluz = 0;
      }
      else if (Retroluz == 0 and reading == 0) {
        lcd.setBacklight(HIGH);
        Retroluz = 1;
      }
      delay(500);
      return;
    }      
    
    // Faz a medicao da temperatura a cada intervalo_medicao
    if (millis() - ultima_medicao > intervalo_medicao)
    {
      digitalWrite(led, HIGH);
      DS18B20.requestTemperatures(); 
      temp = DS18B20.getTempCByIndex(0);
      ultima_medicao = millis();
      Serial.println("Temperatura: " + String(temp) + "    -    Alvo: " + String(temp_alvo) + "    -    Atividade: " + String(atividade) + "    -    Último desligamento do freezer: " + String( (millis() - ultimo_desligamento)/60000 ) + "    -    Timestamp ult desl: " + String( ultimo_desligamento ) + "    -    Modo cervejeira: " + String(cervejeira));

      if (valendo == true){
        // Tomada de decisão quanto a atividade     
          //Se passar do limite da tolerancia pra cima e respeitar o intervalos mínimo de proteção do freezer: liga refrigerador (1).
        if ( (temp > temp_alvo + tolerancia) and ( (millis() - ultimo_desligamento >= intervalo_protecao_freezer ) or (ultimo_desligamento == 0) ) ){ 
          Serial.println("1");
          atividade = 1;
          no_atividade = "Refrigera&ccedil;&atilde;o acionada.";
          alias_atividade = "*";
          digitalWrite(RES_frio, LOW);
          digitalWrite(RES_quente, HIGH);
        }
          //Apenas informativo de que o freezer deveria ser ligado, mas está em modo de proteção do motor.
        else if ( (temp > temp_alvo + tolerancia) and (atividade != 1) and ( (millis() - ultimo_desligamento < intervalo_protecao_freezer ) ) ){ 
          Serial.println("2");
          no_atividade = "Modo de prote&ccedil;&atilde;o do freezer. Tempo restante: " + String( (intervalo_protecao_freezer - (millis() - ultimo_desligamento))/60000 ) + " minuto(s).";
          alias_atividade = "$";
          digitalWrite(RES_frio, HIGH);
          digitalWrite(RES_quente, HIGH);
        }
          //Apenas informativo de que a temperatura retornou ao padrão ideal após passar por um período de proteção.
        else if ( (atividade != 1) and (temp >= temp_alvo - tolerancia) and (temp <= temp_alvo + tolerancia) ){ 
          Serial.println("3");
          no_atividade = "Temperatura dentro da zona de toler&acirc;ncia.";
          alias_atividade = ".";
          digitalWrite(RES_frio, HIGH);
          digitalWrite(RES_quente, HIGH);        
        }
          //Se passar do limite da tolerancia pra baixo: liga aquecedor (-1) [apenas para modo brewery (cervejeira == 0)]
        else if ( (temp < temp_alvo - tolerancia) and (cervejeira == 0) ){
          Serial.println("4");
          if (atividade == 1){
            ultimo_desligamento = millis();
            Serial.println("6");
          } //se foi um desligamento do freezer, guarda o horário
          atividade = -1;
          no_atividade = "Aquecimento acionado.";
          alias_atividade = "#";
          digitalWrite(RES_frio, HIGH);
          digitalWrite(RES_quente, LOW);    
        }
          //Se tiver em alguma atividade (resfriamento ou aquecimento) e passar da temp alvo: desliga tudo (0)
        else if ((temp >= temp_alvo and atividade == -1) or (temp <= temp_alvo and atividade == 1)) {  
          Serial.println("5");
          if (atividade == 1){
            ultimo_desligamento = millis();
            Serial.println("6");
          } //se foi um desligamento do freezer, guarda o horário
          atividade = 0;
          no_atividade = "Temperatura dentro da zona de toler&acirc;ncia.";
          alias_atividade = ".";
          digitalWrite(RES_frio, HIGH);
          digitalWrite(RES_quente, HIGH);        
        }         
      }
      else{ //valendo == false
        atividade = 0;
        no_atividade = "Iniciando...";
        alias_atividade = "-";
        digitalWrite(RES_frio, HIGH);
        digitalWrite(RES_quente, HIGH);                 
      }
      //Se não estiver em atividade e dentro dos limites: continua tudo como está (0)
      digitalWrite(led, LOW);
    }
    

    // Imprime no Display
    //lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Temp Alvo Wf " + status_cervejeira + " " + alias_atividade);
    lcd.setCursor(0,1);
    lcd.print(String(temp,1) + " " + String(temp_alvo*1.0,1) + "  " + status_wifi + "      ");
    lcd.setCursor(15,1);
    lcd.print(alias_programado);
      
    // Monta Site
    MontaWebSite();
    
    // Envia dados ao ThingSpeak
    if (millis() - ultimo_envio > intervalo_TS and WiFi.status() == 3)
    {
      //Serial.println(WiFi.status());
      lcd.setCursor(15,1);
      lcd.print("@");
      digitalWrite(led, HIGH);
      Serial.print("TS...");
      EnviaDadosTS(temp);
      ultimo_envio = millis();
      Serial.print("enviado");
      digitalWrite(led, LOW);
      lcd.setCursor(15,1);
      lcd.print(" ");
    }
    
    // ????
    count = myPeriodic;
    while(count--)
    delay(100);
  }
// FIM MODO 0 --------------------------------------------

// MODO 1 - Configuração de temperatura alvo ------------
  else if (modo == 1){
    
    digitalWrite(led, LOW);
    
    if (solta_botao == 1) { 
      Serial.println("Modo ajuste temperatura alvo");
      lcd.setCursor(0,0);
      lcd.print("Ajuste temp alvo");
      lcd.setCursor(0,1);      
      lcd.print(String(temp_alvo*1.0,1) + "               ");
      Serial.println(String(temp_alvo*1.0,1));
      delay(200);    
      solta_botao = 0;
      delay(500);
      return;
    }

    // aumenta a temperatura alvo ou cold crashing
    if (digitalRead(botao_mais) == LOW) {
      ultimo_tempo = millis();
      while (digitalRead(botao_mais) == LOW) {
        delay(100);
        if ((millis() - ultimo_tempo) > tempo_botao) { //cold crashing 
          Serial.println("cold crashing...");
          lcd.clear();
          lcd.setCursor(0,1);         
          lcd.print("Cold crashing...");      
          delay(1000);
          modo = 0;
          temp_alvo = -0.5;
          EEPROM.begin(4096);
          EEPROM.write(0, abs(temp_alvo));
          EEPROM.write(1, (abs(temp_alvo) - int(abs(temp_alvo))) * 10);
          if (temp_alvo >= 0) {EEPROM.write(8, 0);}
          else {EEPROM.write(8, 1);}
          EEPROM.end();
          return; 
        }
      }
      temp_alvo = temp_alvo + 0.5;
      lcd.setCursor(0,1);      
      lcd.print(String(temp_alvo*1.0,1) + "               ");       
      Serial.println(String(temp_alvo*1.0,1));
      while (digitalRead(botao_mais) == LOW) {
        delay(200);
      }
    }
    
    // diminui temp alvo ou sai do menu ou diminui temperatura alvo
    else if (digitalRead(botao_menos) == LOW) {
      ultimo_tempo = millis();
      while (digitalRead(botao_menos) == LOW) {
        delay(100);
        if ((millis() - ultimo_tempo) > tempo_botao) { //sair do ajuste 
          Serial.println("aguarde...");
          lcd.clear();
          lcd.setCursor(0,0);         
          lcd.print("Aguarde...     ");      
          delay(1000);
          modo = 0;
          EEPROM.begin(4096);
          EEPROM.write(0, temp_alvo);
          EEPROM.write(1, (temp_alvo - int(temp_alvo)) * 10);
          if (temp_alvo >= 0) {EEPROM.write(8, 0);}
          else {EEPROM.write(8, 1);}
          EEPROM.end();
          return;  
        }
      }  
      temp_alvo = temp_alvo - 0.5;
      lcd.setCursor(0,1);
      lcd.print(String(temp_alvo*1.0,1) + "               ");         
      Serial.println(String(temp_alvo*1.0,1));
      delay(200);
    }
  
  }
// FIM MODO 1 --------------------------------------------

}






void MontaWebSite()
{
  String req;
  unsigned long tempo_resposta;
  unsigned long tempo_maximo = 3000;
  String checked_or_not;
  String cerv_ou_brew;
  String tempo_programado_exibir;

  if (alvo_programado == 0) {
    checked_or_not = "";
    tempo_programado_exibir = "0";
  }
  else {
    checked_or_not = "checked";
    tempo_programado_exibir = ((float)tempo_programado_final - (float)millis())/ 3600000;
  }

  if (cervejeira == 0) {
    cerv_ou_brew = "";
  }
  else {
    cerv_ou_brew = "checked";
  }
  
  //Serial.println("tempo programado " + checked_or_not);
  //Serial.println("cervejeira " + cerv_ou_brew);
  
  WiFiClient client = server.available();
  if (!client) {
    Serial.print(".");
    return;
  }


  tempo_resposta = millis();
  while(!client.available()){
    delay(1); 
    if ((millis() - tempo_resposta) > tempo_maximo) {
      Serial.println("tempo maximo esgotado");       
      break;
    } 
  }
  
  if (client.available()) {   
    Serial.println("cliente disponível");
    //boolean currentLineIsBlank = true;
    //boolean sentHeader = false;
    String myStr = "";
//    unsigned long tempo_resposta;
//    unsigned long tempo_maximo = 7000;
    boolean houve_req = false;
    tempo_resposta = millis();
    while (client.connected()) {
      if (client.available()) {
        houve_req = true;
        char c = client.read();
        //Serial.print(c);
        myStr += c;
        
        // Quando é uma requisição de reload (e não de submit)
        if (myStr == "GET") {
          Serial.println("Soliciatação do tipo GET (ignorada)");
          break;
        }
  
        // Vai zerando myStr até chegar na última linha, que é a do POST
        if (c == '\n') {
          myStr = "";
        }      
      }
      
      if (houve_req = true and !client.available()) {
        Serial.println("Houve req do tipo POST e acabou");
        Serial.println(myStr);
        req = myStr;
        break;
      }
      
      if ((millis() - tempo_resposta) > tempo_maximo) {
          Serial.println("tempo maximo esgotado");       
          break;
      }
    }
    //Serial.println("");
    //Serial.println(myStr);
    //parseThangs(myStr);  
    delay(100); // give the web browser time to receive the data
    //client.stop(); // close the connection:    
  }
  
  


  client.flush();
 
  String buf = "";

  /*
  intervalo_TS_min = int(intervalo_TS/60000);
  intervalo_TS-seg = ((intervalo_TS/1000) - int(intervalo_TS/60000)*60)
  intervalo_medicao_min = int(intervalo_medicao/60000);
  intervalo_medicao_seg = ((intervalo_medicao/1000) - int(intervalo_medicao/60000)*60)
  */

  buf += "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
  //buf += "<body background=""https://www.google.com.br/imgres?imgurl=http%3A%2F%2Fwww.elhombre.com.br%2Fwp-content%2Fuploads%2F2014%2F03%2Fcerveja-sem-arte1.jpg&imgrefurl=http%3A%2F%2Fwww.elhombre.com.br%2F10-argumentos-cientificos-para-beber-cerveja-todo-dia%2F&docid=hQIelRN65gFv7M&tbnid=rL-j-k762MOYfM%3A&w=620&h=413&bih=969&biw=1029&ved=0ahUKEwiE8dH_pODOAhVCl5AKHftCASwQMwhdKCAwIA&iact=mrc&uact=8"">";
  //buf += "<head><meta http-equiv='refresh' content='3'>";
  //buf += "<h1><font color=""DarkBlue"">CALEJU</font></h1></head><body>";
  buf += "<h1><font color=""DarkBlue"">CALEJU v.2</font></h1>";

  if (cervejeira == true) { buf += "<h3><font color=""Red"">MODO CERVEJEIRA ATIVADO!</font></h3>"; }
  
  buf += "<table width=""900""><tr>";
  buf += "<td bgcolor=""" + cor_fundo_1 + """ width=""400"" valign=""bottom""><form name=""myform1"" method=""POST""><div align=""left"">";
  buf += "<input type=""checkbox"" name=""modo_cervejeira"" value=""modo_cervejeira"" " + cerv_ou_brew + """ >Modo cervejeira";
  buf += "<p>Temperatura alvo:&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; <input type=""text"" name=""FORM1_alvo"" size=""5"" value=""" + String(temp_alvo) + """>&deg;C<br></p>";
  buf += "<p>Toler&acirc;ncia:&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; <input type=""text"" name=""tol"" size=""5"" value=""" + String(tolerancia) + """>&deg;C<br></p>";
  buf += "<p>Intervalo de medi&ccedil;&atilde;o:&nbsp; <input type=""text"" name=""Md_min"" size=""2"" value=""" + String(int(intervalo_medicao/60000)) + """>min&nbsp;&nbsp;<input type=""text"" name=""Md_seg"" size=""2"" value=""" + String((intervalo_medicao/1000) - int(intervalo_medicao/60000)*60) + """>seg<br></p>";
  buf += "<p>Intervalo ThingSpeak: <input type=""text"" name=""TS_min"" size=""2"" value=""" + String(int(intervalo_TS/60000)) + """>min&nbsp;&nbsp;<input type=""text"" name=""TS_seg"" size=""2"" value=""" + String((intervalo_TS/1000) - int(intervalo_TS/60000)*60) + """>seg<br></p>";
  buf += "<p>Intervalo de prote&ccedil;&atilde;o do freezer: <input type=""text"" name=""protec"" size=""2"" value=""" + String(int(intervalo_protecao_freezer/60000)) + """>min&nbsp;&nbsp;<br></p>";
  buf += "<p><font color=""red"">Senha: <input type=""password"" name=""senha"" size=""10"" value="">*</font><br></p>";
  buf += "<input type=""submit"" value=""enviar"">";
  buf += "</div></form></td>";
  
  buf += "<td></td>";

  if (cervejeira == false) {  //Só aparece alvo programado para o modo brewery
    buf += "<td bgcolor=""" + cor_fundo_2 + """ width=""400px"" valign=""bottom""><form name=""myform2"" method=""POST""><div align=""left"">";
    buf += "<input type=""checkbox"" name=""prog_alvo"" value=""prog_alvo"" " + checked_or_not + """ >Alvo programado";
    buf += "<p>Temperatura programada:&nbsp;&nbsp;<input type=""text"" name=""FORM2_prog_tp"" size=""5"" value=" + String(temperatura_programada) + ">&deg;C<br></p>";
    buf += "<p>Tempo:&nbsp;&nbsp; <input type=""text"" name=""prog_tt"" size=""5"" value=" + tempo_programado_exibir + ">h<br></p>";
    buf += "<p><br><br><font color=""red"">Senha: <input type=""password"" name=""senha"" size=""10"" value="">*</font><br></p>";
    buf += "<input type=""submit"" value=""programar"">";
    buf += "</div></form></td>";
  }

  buf += "</table></tr>";
  
  //buf += "<h2>Temp: """ + String(temp) + """ &deg;C &nbsp;&nbsp;&nbsp; <input type=""button"" value=""atualizar"" onClick=""history.go(0)""> </h2>";
  buf += "<h2>Temp: """ + String(temp) + """ &deg;C &nbsp;&nbsp;&nbsp; <input type=""button"" value=""atualizar"" onClick=""window.location.href=window.location.href""> </h2>";
  buf += "<h5>""" + no_atividade + """</h5>";
  buf += "<iframe width=""450"" height=""260"" style=""border: 1px solid #cccccc;"" src=""https://thingspeak.com/channels/147681/charts/2?bgcolor=%23ffffff&color=%23d62020&days=1&dynamic=true&max=50&min=0&results=180&title=Temperatura+do+fermentador&type=line&xaxis=Tempo&yaxis=Temperatura+%C2%B0C&api_key=TPP9WLZC38BPFI82""></iframe>";
  buf += "<iframe width=""450"" height=""260"" style=""border: 1px solid #cccccc;"" src=""https://thingspeak.com/channels/147681/charts/1?bgcolor=%23ffffff&color=%23d62020&days=1&dynamic=true&results=180&title=Temperatura+alvo&type=line&xaxis=Tempo&yaxis=Temperatura+%C2%B0C&api_key=TPP9WLZC38BPFI82""></iframe>";
  buf += "<iframe width=""450"" height=""260"" style=""border: 1px solid #cccccc;"" src=""https://thingspeak.com/channels/147681/charts/3?bgcolor=%23ffffff&color=%23d62020&days=1&dynamic=true&results=180&title=Atividade&type=line&xaxis=Tempo&yaxis=+&yaxismax=1&yaxismin=-1&api_key=TPP9WLZC38BPFI82""></iframe>";
  buf += "<iframe width=""450"" height=""260"" style=""border: 1px solid #cccccc;"" src=""https://thingspeak.com/channels/174985/charts/3?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=288&title=Temperatura+M%C3%A9dia+%2824h%29&type=line&xaxis=Tempo&yaxis=Temperatura+%C2%B0C&api_key=NWY3E2Z28JO20HQ6""></iframe>";
  if (ultimo_desligamento > 0){
    buf += "<h6>Tempo desde o &uacute;ltimo desligamento do freezer: """ + String((millis() - ultimo_desligamento)/60000) + """ min</h6>";
  }
  buf += "<p><a href=""https://thingspeak.com/channels/147681/private_show"">Ir para ThingSpeak</a></p>";
  buf += "</html>\n";

  Serial.println(buf);

//http://www.slumberjer.com/myardu/2016/05/20/reading-and-writing-to-esp8266-01-eeprom/
//http://blog.startingelectronics.com/html-text-box-to-send-text-to-arduino-web-server/
//https://www.arduino.cc/en/Tutorial/WebClient 
  client.print(buf);
  client.flush();


  String str_web_alvo = (req.substring(req.indexOf("alvo=") + 5, req.indexOf("&tol=")));
  String str_web_tol = (req.substring(req.indexOf("&tol=") + 5, req.indexOf("&Md_min")));
  String str_web_alvo_prog = (req.substring(req.indexOf("prog_tp=") + 8, req.indexOf("&prog_tt")));
  String str_web_tt_prog = (req.substring(req.indexOf("prog_tt=") + 8, req.indexOf("&senha")));
  str_web_alvo.replace(",",".");
  str_web_tol.replace(",",".");
  str_web_alvo_prog.replace(",",".");
  str_web_tt_prog.replace(",",".");
  float web_alvo = (str_web_alvo).toFloat();
  float web_tol = (str_web_tol).toFloat();
  float web_alvo_prog = (str_web_alvo_prog).toFloat();
  float web_tt_prog = (str_web_tt_prog).toFloat();
  unsigned short web_Md_min = (req.substring(req.indexOf("&Md_min=") + 8, req.indexOf("&Md_seg"))).toInt();
  unsigned short web_Md_seg = (req.substring(req.indexOf("&Md_seg=") + 8, req.indexOf("&TS_min"))).toInt();
  unsigned short web_TS_min = (req.substring(req.indexOf("&TS_min=") + 8, req.indexOf("&TS_seg"))).toInt();
  unsigned short web_TS_seg = (req.substring(req.indexOf("&TS_seg=") + 8, req.indexOf("&protec"))).toInt();
  unsigned short web_protec = (req.substring(req.indexOf("&protec=") + 8, req.indexOf("&senha"))).toInt();
  boolean modo_cervejeira_local; //cervejeira ou brewery
  if(req.indexOf("modo_cervejeira") != -1) {modo_cervejeira_local = 1;}
  else {modo_cervejeira_local = 0;}

  //Serial.println("EEPROM(10): " + String(EEPROM.read(12)));
  //Serial.println("index: " + String(req.indexOf("modo_cervejeira")));
  //Serial.println("modo local: " + String(modo_cervejeira_local));
  //Serial.println("global: " + String(cervejeira));

  
  if (req.indexOf("clj2015") != -1)
  {
    if (req.indexOf("FORM1") != -1)
    {
      if (modo_cervejeira_local != cervejeira){
        Serial.println("Modo brewery ou cervejeira modificado");
        //cervejeira = modo_cervejeira_local;
        alternaCervejeiraBrewery();
      }
      else {

        if (web_alvo != temp_alvo) {
          Serial.println("temperatura alvo modificada");
          temp_alvo = web_alvo;
          EEPROM.begin(4096);     
          EEPROM.write(0, abs(temp_alvo));
          EEPROM.write(1, (abs(temp_alvo) - int(abs(temp_alvo))) * 10);
          if (temp_alvo >= 0) {EEPROM.write(8, 0);}
          else {EEPROM.write(8, 1);}     
          EEPROM.end();         
        }
        if (web_tol != tolerancia) {
          Serial.println("tolerancia modificada");      
          tolerancia = abs(web_tol);
          EEPROM.begin(4096);     
          EEPROM.write(6, tolerancia);
          EEPROM.write(7, (tolerancia - int(tolerancia)) * 10);
          EEPROM.end();         
        }    
        if ((web_Md_min * 60 + web_Md_seg) * 1000 != intervalo_medicao) {
          Serial.println("intervalo medicao modificado");      
          intervalo_medicao = (abs(web_Md_min) * 60 + abs(web_Md_seg)) * 1000;
          EEPROM.begin(4096);     
          EEPROM.write(4, web_Md_min);
          EEPROM.write(5, web_Md_seg);
          EEPROM.end();         
        }  
        if ((web_TS_min * 60 + web_TS_seg) * 1000 != intervalo_TS) {
          Serial.println("intervalo TS modificado");      
          intervalo_TS = (abs(web_TS_min) * 60 + abs(web_TS_seg)) * 1000;
          EEPROM.begin(4096);     
          EEPROM.write(2, web_TS_min);
          EEPROM.write(3, web_TS_seg);
          EEPROM.end();         
        }
        if (web_protec*60*1000 != intervalo_protecao_freezer) {
          Serial.println("intervalo de proteção do freezer modificado para " + String(web_protec) + " min" );
          intervalo_protecao_freezer = web_protec*60*1000;
          EEPROM.begin(4096);
          EEPROM.write(12, web_protec);
          EEPROM.end();         
          Serial.println("EEPROM depois do end: " + String(EEPROM.read(12)));
        }       
      }
 
    }
     if (req.indexOf("FORM2") != -1)
    {    
      Serial.print("index é ");
      Serial.println(req.indexOf("prog_alvo"));
      //apenas quando o check box do alvo programada estiver marcado e da vez anterior estava desmarcado
      if ( req.indexOf("prog_alvo") != -1 )
      {
        cor_fundo_2="#FFE4E1";
        alvo_programado = 1;
        alias_programado = "P";
        temperatura_programada = web_alvo_prog;
        tempo_programado = web_tt_prog * 3600000;
          if (millis() + tempo_programado <= 4294967295) { //maximo suportado por millis (unsigned long) [49,7 dias]
            tempo_programado_final = millis() + tempo_programado; 
          }
          else {
            tempo_programado_final = millis() + tempo_programado - 4294967295;
          }
      }
      else
      {
        cor_fundo_2="#F0F8FF";
        alvo_programado = 0;
        temperatura_programada = 0;
        tempo_programado = 0;
        alias_programado = "";
      }
    }
  }
  delay(100);
  client.stop();
  Serial.println("Cliente desligado");
}


void conectaWifi()
{ 
  Serial.print("Verificando conexao com rede Wifi... ");
  int i = 1;
  //WiFi.disconnect();
  WiFi.begin(rede, senha);  
  while (WiFi.status() != WL_CONNECTED and (i < 15)) 
  {
    delay(500);
    Serial.print(".");
    i = i + 1;
  }
  if (i < 15)  {Serial.println("Conectado!");}
  else {Serial.println("Não foi possível conectar a rede Wifi!!!");}
}



void EnviaDadosTS(float temp)
{  
  const char* TS_server = "api.thingspeak.com";
  String apiKey ="3Q0MIIB7Z4MKTDZM";
  
  WiFiClient client;
  
  if (client.connect(TS_server, 80)) { // use ip 184.106.153.149 or api.thingspeak.com
    Serial.print("WiFi Client connected...  ");
     
    String postStr = apiKey;
    postStr += "&field1=";
    postStr += String(temp_alvo);
    postStr += "&field2=";
    postStr += String(temp);
    postStr += "&field3=";
    postStr += String(atividade);
    postStr += "\r\n\r\n";
     
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    Serial.println("Dados enviados ao ThingSpeak!");
  }
 client.stop();
}




void alternaCervejeiraBrewery()
{
  if (cervejeira == 0) {
    cervejeira = 1;
    Serial.println("modo cervejeira...");
    lcd.clear();
    lcd.setCursor(0,1);
    lcd.print("Modo cervejeira...");      
    delay(1000);
    temp_alvo = temp_padrao_cervejeira;
    //ultimo_desligamento = 0;
    EEPROM.begin(4096);
    EEPROM.write(0, abs(temp_alvo));
    EEPROM.write(1, (abs(temp_alvo) - int(abs(temp_alvo))) * 10);
    if (temp_alvo >= 0) {EEPROM.write(8, 0);}
    else {EEPROM.write(8, 1);}
    EEPROM.write(9, 1);
    EEPROM.end();
    reading = 1;
    cor_fundo_1 = "#FGFG96";
    return;
  }
  else{
    cervejeira = 0;
    Serial.println("modo brewery...");
    lcd.clear();
    lcd.setCursor(0,1);         
    lcd.print("Modo brewery...");      
    delay(1000);
    temp_alvo = 18;
    //ultimo_desligamento = 0;
    EEPROM.begin(4096);
    EEPROM.write(0, abs(temp_alvo));
    EEPROM.write(1, (abs(temp_alvo) - int(abs(temp_alvo))) * 10);
    if (temp_alvo >= 0) {EEPROM.write(8, 0);}
    else {EEPROM.write(8, 1);}            
    EEPROM.write(9, 0);
    EEPROM.end();
    reading = 1;
    cor_fundo_1 = "#F0F8FF";
  }
}
