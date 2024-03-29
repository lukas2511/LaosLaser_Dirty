/*
 * main.cpp
 * Laos Controller, main function
 *
 * Copyright (c) 2011 Peter Brier & Jaap Vermaas
 *
 *   This file is part of the LaOS project (see: http://wiki.laoslaser.org)
 *
 *   LaOS is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   LaOS is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with LaOS.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This program consists of a few parts:
 *
 * ConfigFile   Read configuration files
 * EthConfig    Initialize an ethernet stack, based on a configuration file (includes link status monitoring)
 * LaosDisplay  User interface functions (read keys, display text and menus on LCD)
 * LaosMenu     User interface stuctures (menu navigation)
 * LaosServer   TCP/IP server, accept connections read/write data
 * LaosMotion   Motion control functions (X,Y,Z motion, homing)
 * LaosIO       Input/Output functions
 * LaosFile     File based jobs (read/write/delete)
 *
 * Program functions:
 * 1) Read config file
 * 2) Enable TCP/IP stack (Fixed ip or DHCP)
 * 3) Instantiate tcp/ip port and accept connections
 * 4) Show menus, peform user actions
 * 5) Controll laser
 * 6) Controll motion
 * 7) Set and read IO, check status (e.g. interlocks)
 *
 */
#include "global.h"
#include "ConfigFile.h"
#include "EthConfig.h"
#include "TFTPServer.h"
#include "LaosMenu.h"
#include "LaosMotion.h"
#include "SDFileSystem.h"
#include "laosfilesystem.h"

// MBED blue status leds
DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

// Status and communication
DigitalOut eth_link(p29); // green
//DigitalOut eth_speed(p30); // yellow
EthernetNetIf *eth; // Ethernet, tcp/ip

// Filesystems
LocalFileSystem local("local");   //File System
LaosFileSystem sd(p11, p12, p13, p14, "sd");

// Laos objects
LaosDisplay *dsp;
LaosMenu *mnu;
TFTPServer *srv;
LaosMotion *mot;
Timer systime;

// Config
GlobalConfig *cfg;

// Protos
void GetFile(void);
void main_nodisplay();
void main_menu();

// for debugging:
extern void plan_get_current_position_xyz(float *x, float *y, float *z);
extern PwmOut pwm;
extern "C" void mbed_reset();

// Safety Disabler
DigitalOut safetyenabler(p30);

/**
*** Main function
**/
int main()
{
  safetyenabler=1;
  systime.start();
  //float x, y, z;
//  eth_speed = 1;

  dsp = new LaosDisplay();
  printf( VERSION_STRING "...\r\nBOOT...\r\n" );
  mnu = new LaosMenu(dsp);
//  eth_speed=0;

 printf("TEST SD...\r\n");
  FILE *fp = sd.openfile("test.txt", "wb");
  if ( fp == NULL )
  {
    mnu->SetScreen("WAITING FOR SD..PLEASE WAIT");
    wait(2.0);
    mbed_reset();
  }
  else
  {
    printf("SD: READY...\r\n");
    fclose(fp);
    removefile("test.txt");
  }

  // See if there's a .bin file on the SD
  // if so, put it on the MBED and reboot
  if (SDcheckFirmware()) mbed_reset();

  mnu->SetScreen(VERSION_STRING);
  printf("START...\r\n");
  cfg =  new GlobalConfig("config.txt");
  mnu->SetScreen("CONFIG OK....");
  printf("CONFIG OK...\r\n");
  if (!cfg->nodisplay)
    dsp->testI2C();

  printf("MOTION...\r\n");
  mot = new LaosMotion();

  eth = EthConfig();
//  eth_speed=1;

  printf("SERVER...\r\n");
  srv = new TFTPServer("/sd", cfg->port);
  mnu->SetScreen("SERVER OK....");
  wait(0.5);
  mnu->SetScreen(9); // IP
  wait(1.0);

  printf("RUN...\r\n");

  // Wait for key, and then home

  if ( cfg->autohome )
  {
    printf("WAIT FOR COVER...\r\n");
    wait(1);


  // Start homing
    mnu->SetScreen("WAIT FOR COVER....");
    //if ( cfg->waitforstart )
      while ( !mot->isStart() );
    mnu->SetScreen("HOME....");
    printf("HOME...\r\n");

    mot->home(cfg->xhome,cfg->yhome, cfg->zhome);
    // if ( !mot->isHome ) exit(1);
    printf("HOME DONE. (%d,%d, %d)\r\n",cfg->xhome,cfg->yhome,cfg->zhome);
  }
  else
    printf("Homing skipped: %d\r\n", cfg->autohome);

  // clean sd card?
  if (cfg->cleandir) cleandir();
  mnu->SetScreen(NULL);

  if (cfg->nodisplay) {
    printf("No display set\r\n");
    main_nodisplay();
  } else {
    printf("Entering display\r\n");
    main_menu();
  }
}

void main_nodisplay() {
  float x, y, z = 0;

  // main loop
   while(1)
  {
    led1=led2=led3=led4=0;
    mnu->SetScreen("Wait for file ...");
    while (srv->State() == listen)
        Net::poll();
    GetFile();
    mot->reset();
    plan_get_current_position_xyz(&x, &y, &z);
     printf("%f %f\r\n", x,y);
    mnu->SetScreen("Laser BUSY...");

    char name[32];
    srv->getFilename(name);
    printf("Now processing file: '%s'\r\n", name);
    FILE *in = sd.openfile(name, "r");
    while (!feof(in))
    {
      while (!mot->ready() );
      mot->write(readint(in),MODE_RUN);
    }
    fclose(in);
    removefile(name);
    // done
    printf("DONE!...\r\n");
	while (!mot->ready() );
    mot->moveTo(cfg->xrest, cfg->yrest, cfg->zrest);
  }
}


void main_menu() {
  // main loop
  while (1) {
        led1=led2=led3=led4=0;

        mnu->SetScreen(1);
        while (1) {;
            mnu->Handle();
            Net::poll();
            if (srv->State() != listen) {
                GetFile();
                char myname[32];
                srv->getFilename(myname);
                if (isFirmware(myname)) {
                    installFirmware(myname);
                    mnu->SetScreen(1);
                } else {
                    if (strcmp("config.txt", myname) == 0) {
                        // it's a config file!
                        mnu->SetScreen(1);
                    } else {
                        if (isLaosFile(myname)) {
                            mnu->SetFileName(myname);
                            mnu->SetScreen(2);
                        }
                    }
                }
            }
        }
    }
}

/**
*** Get file from network and save on SDcard
*** Ascii data is read from the network, and saved on the SD card in binary int32 format
**/
void GetFile(void) {
   Timer t;
   printf("Main::GetFile()\r\n" );
   mnu->SetScreen("Receive file...");
   t.start();
   while (srv->State() != listen) {
     Net::poll();
     switch ((int)t.read()) {
        case 1:
            mnu->SetScreen("Receive file");
            break;
        case 2:
            mnu->SetScreen("Receive file.");
            break;
        case 3:
            mnu->SetScreen("Receive file..");
            break;
        case 4:
            mnu->SetScreen("Receive file...");
            t.reset();
            break;
     }
   }
   mnu->SetScreen("Received file.");
} // GetFile

