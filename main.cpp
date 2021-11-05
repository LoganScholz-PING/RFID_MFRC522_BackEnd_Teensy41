#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>

// MFRC522 Vars
#define RST_PIN         9
#define SS_PIN          10

MFRC522 mfrc522(SS_PIN, RST_PIN);

// serial comm vars
#define SOP '<' // denotes start of serial data packet
#define EOP '>' // denotes end of serial data packet

char ctrlChar;
char serialData[20];     // serial receive buffer
byte idx;              // serialData indexer
boolean started = false; // serial data flow control
boolean ended   = false;   // serial data flow control


void setup() 
{
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();
}


/* ***********************************************************
 * FUNCTION NAME: readRFID()
 *    
 * SUMMARY: read the workorder block from the RFID (if present)
 * (workorder is stored in block 1, bytes 0-6)
 * ***********************************************************/
void readRFID()
{
  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  bool skipnormalconn = false;
  
  MFRC522::StatusCode status;
  MFRC522::StatusCode wakeupstatus;

  byte bufferATQA1[2];
  byte bufferSize1 = 2;
  byte buffer2[18]; // must be minimum 18 for MIFARE_Read() -- 16 bytes for block and 2 bytes for CRC
  byte len = 18;
  byte block;

  // *************** CARD WAKE-UP AND RE-SELECTION *************************
  // *************** (try to move HALT cards to READY* state) **************
  // ***** HALT -> READY* = WUPA -> SELECT (should be READY* after SELECT) *
  // ***********************************************************************

  // Look for new cards and attempt to wake up sleeping cards
  if ( ! mfrc522.PICC_IsNewCardPresent() ) 
  {
    // card may be in HALT state so let's try to wake it up
    // if this fails there is probably no card present on the reader
    wakeupstatus = mfrc522.PICC_WakeupA(bufferATQA1, &bufferSize1);

    if ( wakeupstatus == MFRC522::STATUS_OK )
    {
      //Serial.println("[INFO] PICC_WakeupA status OK! Attempting to Select.");
      wakeupstatus = mfrc522.PICC_Select(&(mfrc522.uid), mfrc522.uid.size);

      if ( ! wakeupstatus == MFRC522::STATUS_OK )
      {
        Serial.print(F("[ERROR] Failed to wake up tag. Status code = "));
        Serial.println(mfrc522.GetStatusCodeName(wakeupstatus));
        return;
      }
      else { skipnormalconn = true; }
    }
    else
    {
      Serial.println(F("[INFO] Attempt to read but no working tag present!"));
      return;
    }
  }
  // *************** END card HALT->READY* state transition **************


  // Select one of the cards
  if (! skipnormalconn)
  {
    if ( ! mfrc522.PICC_ReadCardSerial()) 
    {
      Serial.println(F("[INFO] PICC_ReadCardSerial() Failed!"));
      return;
    }
  }
  
  //---------------------------------------- GET WORKORDER

  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid)); //line 834
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  block = 1;

  status = mfrc522.MIFARE_Read(block, buffer2, &len);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  if (sizeof(buffer2) > 0)
  {
    char wo[11];

    // we are here because the read operation was successful
    // and buffer2 contains data (buffer2 contains all 16 bytes of block 1 + 2 byte CRC)
    //    ** the only bytes of buffer2 we care about are 0-6 ** 

    // start building response serial packet:
    wo[0] = '<';
    wo[1] = 'D';
    wo[2] = ':';

    for (uint8_t j = 0; j < 7; j++)
    {
      // TODO(IDEA): Check if buffer2[j] value is between: 
      //    DEC 48 (HEX 0x30) [ASCII 0] and DEC 57 (HEX 0x39) [ASCII 9]         
      if (buffer2[j] == 32 || buffer2[j] == 0) // dec 32 = hex 0x20 = ASCII "SPACE"  || dec 0 = hex 0x00 = NULL
      {
        // get rid of blanks, NULLS, or other non-standard ASCII characters
        //  - replace them with a highly visible ASCII character so C#
        //    can easily check for this on the front-end
        wo[j+3] = '#';
      }
      else
      {
        // data is normal (hopefully), throw it in our workorder char array
        wo[j+3] = (char)buffer2[j];
      }
    } 

    // finish the Tx packet and prepare to send to C# over serial comm
    wo[10] = (char)'>';

    for (uint8_t i = 0; i < 11; i++)
    {
      Serial.write((byte)wo[i]);
    }

    // use the decimal value of the ASCII character
    // (we need to manually insert CR/LF to the serial
    // packet because Serial.write() does not automatically
    // insert them like Serial.println() does)
    //
    // Note that we can probably just use Serial.println(); 
    // here, but I want to experiment
    Serial.write(13); // 13 = CR
    Serial.write(10); // 10 = LF
  }
  else
  {
    Serial.println(F("[ISSUE] buffer2 <= 0 in READ operation! How??"));
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}






/* ***********************************************************
 * FUNCTION NAME: writeRFID()
 *    
 *    SUMMARY: write a 7 digit workorder to block1 
 * ***********************************************************/
void writeRFID(char in_wo[])
{
  bool skipnormalconn = false;
  byte bufferATQA1[2];
  byte bufferSize1 = 2;
  
  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) 
  {
    // card may be in HALT state so let's try to wake it up
    // if this fails there is probably no card present on the reader
    MFRC522::StatusCode wakeupstatus = mfrc522.PICC_WakeupA(bufferATQA1, &bufferSize1);

    if ( wakeupstatus == MFRC522::STATUS_OK )
    {
      //Serial.println("[INFO] PICC_WakeupA status OK! Attempting to Select.");
      wakeupstatus = mfrc522.PICC_Select(&(mfrc522.uid), mfrc522.uid.size);

      if ( ! wakeupstatus == MFRC522::STATUS_OK )
      {
        Serial.print(F("[ERROR-Write] Failed to wake up tag. Status code = "));
        Serial.println(mfrc522.GetStatusCodeName(wakeupstatus));
        return;
      }
      else { skipnormalconn = true; }
    }
    else
    {
      Serial.println(F("[INFO-Write] Attempt to read but no working tag present!"));
      return;
    }
  }

  if (!skipnormalconn)
  {
    // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial()) 
    {
      Serial.println("[ISSUE-Write] PICC_ReadCardSerial() Failed!");
      return;
    }
  }

  byte buffer[18];
  byte block;
  MFRC522::StatusCode status;

  for (byte i = 0; i < 16; ++i)
  {
    if (i < 7) // load the workorder data first
    {
      buffer[i] = in_wo[i];
    }
    else // pad the rest of block 1 with spaces (hex 0x20, decimal 32)
    {
      buffer[i] = ' ';
    }
  }

  block = 1;
  ////Serial.println(F("Authenticating using key A..."));
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) 
  {
    Serial.print(F("PCD_Authenticate() (block 1) failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  // Write block
  status = mfrc522.MIFARE_Write(block, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Write() workorder to block 1 failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  mfrc522.PICC_HaltA(); // Halt PICC
  mfrc522.PCD_StopCrypto1();  // Stop encryption on PCD
}










void checkSerial() {
  // if serial is available, receive the
  // serial data packet (it better be formatted
  // correctly!!)
  while (Serial.available() > 0) {
    char inChar = Serial.read();
    // check if we receive the start character  
    // (SOP) of a serial packet
    if (inChar == SOP) {
      idx = 0;
      serialData[idx] = '\0'; // null character
      started = true;
      ended = false;
    }
    // check if we receive the end character
    // (EOP) of a serial packet
    else if (inChar == EOP) {
      ended = true;
      break;
    }
    else
    {
      if (idx < 20) {
        serialData[idx] = inChar;
        ++idx;
        serialData[idx] = '\0';
      }
    }
  }
  if (started && ended) {
    // packet start and end control characters
    // received, begin packet processing
    switch (serialData[0]) {
      case 'W':
      {
        char rxWO[7];
        int count = 0;

        for(int q = 0; q < 7; ++q)
        {
          if(serialData[q+2] == '\0')
          {
            break;
          }
          else
          {
            rxWO[q] = (char)serialData[q+2];
            count = q;
          }
        }

        rxWO[count+1] = '\0'; // add the escape character to the end

        writeRFID(rxWO);
        break;
      }
      case 'R':
        // READ W/O FROM CARD
        readRFID();
        break;
      default:
        // we received a packet where serialData[0] 
        // is unrecognized
        //Serial.print("ERROR Invalid or uninterpretable command received on serial: ");
        //Serial.println(serialData[0]);
        break;
    }
    // packet processing completed, reset packet
    // parameters to get ready for next packet
    started = false;
    ended = false;
    idx = 0;
    //serialData[idx] = '\0';
    for (byte i = 0; i < sizeof(serialData); ++i)
    {
      serialData[i] = '\0';
    }
  }
}


void loop() 
{
  //
  checkSerial();

  // if you don't do the 2 lines below then the Teensy wastes ~ 30-40mA continuously and gets hot
  // when you don't do these things Teensy uses 110-120mA continuously
  // when you DO these things the Teensy uses 80-90mA continuously (everything still works as normal)
  mfrc522.PICC_HaltA(); 
  mfrc522.PCD_StopCrypto1();
}