
// Ver. 1.3.0

// 1-5-3-6-2-4 Common
// 1-5-4-6-2-3
// 1-2-4-6-5-3
// 1-2-3-6-5-4
//https://www.youtube.com/watch?v=ss0GMKBYCks
// 1-3-4-2 Common
// 1-2-4-3

#if defined ARDUINO_SAM_DUE
#include <FreeRTOS_ARM.h>
#else
#include <FreeRTOS_AVR.h>
#endif

#include <Wire.h>
#include <SPI.h>
#include <CleO.h>
#include <TimerOne.h> // O2
#include <TimerThree.h>  // inyectores

#define SCREEN_WIDTH     800
#define SCREEN_HEIGHT    480
#define pulso_arbol_levas 40
#define pulso_ciguennal 39
#define cuatro_cilindros 0
#define seis_cilindros 1
#define ocho_cilindros 2
#define pulso_O2 34
#define encoderPin2 18
#define encoderPin3 19
#define encoderSwitchPin1 4

void vPeriodicTask1(void *pvParameters);
void vPeriodicTask2(void *pvParameters);
void vPeriodicTask3(void *pvParameters);
void vPeriodicTask4(void *pvParameters);
void vPeriodicTask5(void *pvParameters);

TaskHandle_t xHandle_1, xHandle_2, xHandle_3, xHandle_4, xHandle_5;

volatile int lastEncoded1 = 1;
volatile long value1 = 10 << 2;
int16_t tag, tag2, tiempo_inyeccion_us = 5000;
uint16_t RPM, RPM_OLD;
uint32_t vueltas;
byte cuenta, cuenta2, estado = cuatro_cilindros, cursor = 0;
byte orden_4cil[][4] = { { 49,47,46,48 },{ 49,48,47,46 } };
byte orden_6cil[][6] = { { 49,45,46,44,48,47 },{ 49,45,47,44,48,46 } ,{ 49,45,47,44,48,46 } ,{ 49,45,47,44,48,46 },{ 49,48,47,46,45,44 } };
byte orden_8cil[][8] = { { 49,47,46,48,45,43,42,44 } ,{ 49,48,47,46,45,44,43,42 } };
char motor_4[][8] = { "1-3-4-2" ,"1-2-3-4" };
char motor_6[][12] = { "1-5-3-6-2-4","1-5-4-6-2-3","1-2-4-6-5-3","1-2-3-6-5-4","1-2-3-4-5-6" };
char motor_8[][16] = { "1-3-4-2-1-3-4-2","1-2-3-4-1-2-3-4" };
byte last_timer;
boolean motor_on = false, O2_ON = false;
byte frecuencia_O2 = 10;

int nsamples;
static float samples[3][32] = { 0.0 };

void setup()
{
	analogReference(EXTERNAL);
	Serial.begin(9600);
	Timer1.initialize(500000 / frecuencia_O2); // O2
	Timer3.initialize((16500 / 100) * 6); // para 6 grados y 100 rpm

	 // Encoder
	pinMode(encoderSwitchPin1, INPUT_PULLUP);
	pinMode(encoderPin2, INPUT);
	pinMode(encoderPin3, INPUT);
	attachInterrupt(digitalPinToInterrupt(encoderPin2), updateEncoder1, CHANGE); //Der.
	attachInterrupt(digitalPinToInterrupt(encoderPin3), updateEncoder1, CHANGE); //Izq.

	// Initialize CleO - needs to be done only once
	CleO.begin();
	// Get the handle for the image
	int16_t im = CleO.LoadImageFile("Pictures/Begas.jpg", 0);
	// Start building a screen frame
	CleO.Start();
	// Draw a bitmap at (0, 0) using im handle
	CleO.Bitmap(im, 0, 0);
	// Display completed screen frame
	CleO.Show();

	if (!digitalRead(encoderSwitchPin1)) {

		/* select Sound, pitch, volume and duration */
		CleO.SetSound(CHIMES, 60, 40, 0);
		/* play the sound */
		CleO.SoundPlay(1);
		//CleO.MPlay(PIANO, 400, "E5A5E5B5E5G5A5E5C6E5D6E5B5C6E5A5E5B5E5G5A5E5C6E5D6E5B5C6E5B5");
		CleO.AudioPlay("@Music/stinger.wav", PLAY_ONCE);
	}

	// Pines salida
	for (int i = 0; i<19; i++)
		pinMode(31 + i, OUTPUT);

	// Introducción
	for (int i = 0; i<19; i++) {
		delay(75);
		digitalWrite(31 + i, HIGH);
	}   // turn the LED on (HIGH is the voltage level)

	for (int i = 0; i<19; i++) { // wait for a second
		delay(75);
		digitalWrite(31 + i, LOW);
	}// turn the LED off by making the voltage LOW

	 // Creación tramas
	if (xTaskCreate(vPeriodicTask1, "P. pantalla", 200, NULL, tskIDLE_PRIORITY + 1, &xHandle_1) == pdPASS) { vTaskSuspend(xHandle_1); }
	if (xTaskCreate(vPeriodicTask2, "Parametros", 200, NULL, tskIDLE_PRIORITY + 1, &xHandle_2) == pdPASS) { NULL; }  // Menús
	if (xTaskCreate(vPeriodicTask3, "Voltimetros", 200, NULL, tskIDLE_PRIORITY + 1, &xHandle_3) == pdPASS) { vTaskSuspend(xHandle_3); } // Voltímetros
	if (xTaskCreate(vPeriodicTask4, "Motor", 200, NULL, tskIDLE_PRIORITY + 1, &xHandle_4) == pdPASS) { vTaskSuspend(xHandle_4); } //Motor
	if (xTaskCreate(vPeriodicTask5, "Arbol de levas", 200, NULL, tskIDLE_PRIORITY + 1, &xHandle_5) == pdPASS) { vTaskSuspend(xHandle_5); } // O2

	vTaskStartScheduler();
	for (;;);
}

////////////////////////////////////////////////////////////
// Protector de pantalla
////////////////////////////////////////////////////////////
void vPeriodicTask1(void *pvParameters)
{

	TickType_t xLastWakeTime = xTaskGetTickCount();
	delay(1000);

	static int minX = 75, minY = 15;
	static int maxX = 800 - minX, maxY = 480 - minY;
	static int startX = maxX / 2, startY = maxY / 2;
	static int deltaX = 2, deltaY = 2;

	for (;;)
	{
		startX = startX + deltaX;   startY = startY + deltaY;

		/* Take care of screen boundary cases */
		if (startX >= maxX)     deltaX = -deltaX;
		if (startX <= minX)     deltaX = abs(deltaX);
		if (startY >= maxY)     deltaY = -deltaY;
		if (startY <= minY)     deltaY = abs(deltaY);

		CleO.Start();

		/* Draw the string and update its coordinates */
		CleO.StringExt(FONT_MEDIUM, startX, startY, LIGHT_GREEN, MM, 0, 0, "Begas Motor");

		/* Display completed screen frame */
		CleO.Show();

		if (!digitalRead(encoderSwitchPin1)) {

			vTaskResume(xHandle_2);  // activo multiplot
			vTaskSuspend(xHandle_1); // paro menús}
		}

		vTaskDelayUntil(&xLastWakeTime, (50 / portTICK_PERIOD_MS));
	}
}

void clear_leds()
{
	cuenta = 0;
	for (int i = 0; i<19; i++) digitalWrite(31 + i, 0);
}

void timer_O2() {

	digitalWrite(pulso_O2, digitalRead(pulso_O2) ^ 1);
}

void timer_4cil()
{
	if (cuenta == 0) {
		digitalWrite(orden_4cil[cursor][0], 1); digitalWrite(pulso_ciguennal, 1); digitalWrite(pulso_arbol_levas, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_4cil[cursor][0], 0); digitalWrite(pulso_ciguennal, 0); digitalWrite(pulso_arbol_levas, 0);
		vueltas += 2;
	}

	else if (cuenta == 30) { digitalWrite(orden_4cil[cursor][1], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_4cil[cursor][1], 0); }

	else if (cuenta == 60) {
		digitalWrite(orden_4cil[cursor][2], 1); digitalWrite(pulso_ciguennal, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_4cil[cursor][2], 0); digitalWrite(pulso_ciguennal, 0);
	}

	else if (cuenta == 90) { digitalWrite(orden_4cil[cursor][3], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_4cil[cursor][3], 0); }

	if (cuenta++ == 120) cuenta = 0;

}

void timer_6cil()
{
	// Toggle LED
	if (cuenta == 0) {
		digitalWrite(orden_6cil[cursor][0], 1); digitalWrite(pulso_arbol_levas, 1); digitalWrite(pulso_ciguennal, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_6cil[cursor][0], 0); digitalWrite(pulso_arbol_levas, 0); digitalWrite(pulso_ciguennal, 0);
		vueltas += 2;
	}

	else if (cuenta == 20) { digitalWrite(orden_6cil[cursor][1], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_6cil[cursor][1], 0); }

	else if (cuenta == 40) { digitalWrite(orden_6cil[cursor][2], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_6cil[cursor][2], 0); }

	else if (cuenta == 60) {
		digitalWrite(orden_6cil[cursor][3], 1); digitalWrite(pulso_ciguennal, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_6cil[cursor][3], 0); digitalWrite(pulso_ciguennal, 0);
	}

	else if (cuenta == 80) { digitalWrite(orden_6cil[cursor][4], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_6cil[cursor][4], 0); }

	else if (cuenta == 100) { digitalWrite(orden_6cil[cursor][5], 1); delayMicroseconds(tiempo_inyeccion_us); digitalWrite(orden_6cil[cursor][5], 0); }

	if (cuenta++ == 120) cuenta = 0;
}

void timer_8cil()
{
	// Toggle LED

	if (cuenta == 0) {
		digitalWrite(orden_8cil[cursor][0], 1);  digitalWrite(orden_8cil[cursor][4], 1); digitalWrite(pulso_arbol_levas, 1); digitalWrite(pulso_ciguennal, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_8cil[cursor][0], 0); digitalWrite(orden_8cil[cursor][4], 0);  digitalWrite(pulso_arbol_levas, 0); digitalWrite(pulso_ciguennal, 0);
		vueltas += 2;
	}

	else if (cuenta == 30) {
		digitalWrite(orden_8cil[cursor][1], 1);  digitalWrite(orden_8cil[cursor][5], 1); delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_8cil[cursor][1], 0);  digitalWrite(orden_8cil[cursor][5], 0);
	}

	else if (cuenta == 60) {
		digitalWrite(orden_8cil[cursor][2], 1);  digitalWrite(orden_8cil[cursor][6], 1); digitalWrite(pulso_ciguennal, 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_8cil[cursor][2], 0);  digitalWrite(orden_8cil[cursor][6], 0);  digitalWrite(pulso_ciguennal, 0);
	}

	else if (cuenta == 90) {
		digitalWrite(orden_8cil[cursor][3], 1);  digitalWrite(orden_8cil[cursor][7], 1);
		delayMicroseconds(tiempo_inyeccion_us);
		digitalWrite(orden_8cil[cursor][3], 0);  digitalWrite(orden_8cil[cursor][7], 0);
	}

	if (cuenta++ == 120) cuenta = 0;
}

char* read_Ax(uint16_t Ax) {

	static int16_t mv;
	static int16_t valores_old_Ax[5];
	static char buf[sizeof("%d. %02d")];
	static byte key = 0;

	mv = map(analogRead(Ax), 0, 1023, 0, 4807);
	sprintf(buf, "%d. %02d", mv / 1000, mv % 1000);

	/*for (int i = A0; i < A5; i++) {

	if (Ax == i) { if (abs(mv - valores_old_Ax[i - A0]) > 150) { if (key>5) {
	vTaskResume(xHandle_3);  vTaskSuspend(xHandle_2);//        CleO.SetSound(PIPS(10), 60, 30, 0);    CleO.SoundPlay(1);
	} } valores_old_Ax[i - A0] = mv; key++; }
	}       */

	return buf;
}

const char* print_number(uint32_t Ax) {

	static char buf[9];
	sprintf(buf, "%d", Ax);
	return buf;
}

// Rescale x from the range (x0, x1) to the range (y0, y1)
float rescale(int x, int x0, int x1, int y0, int y1) {
	x = max(x0, min(x1, x));
	return y0 + ((x - x0) * long(y1 - y0) / float(x1 - x0));
}

float gauge_angle(int x) {
	return rescale(x, 0, 1023, 180 - 45, 180 + 44);
}

void menu()
{
	motor_on = true;
	switch (estado)
	{
	case cuatro_cilindros:Timer3.attachInterrupt(timer_4cil); // attach the service routine here
		break;
	case seis_cilindros:Timer3.attachInterrupt(timer_6cil); // attach the service routine here
		break;
	case ocho_cilindros:Timer3.attachInterrupt(timer_8cil); // attach the service routine here
		break;
	default:
		break;
	}
}

//////////////////////////////////////////////////////////////////////
//  Parámetros
//////////////////////////////////////////////////////////////////////
void vPeriodicTask2(void *pvParameters)
{
	delay(1000);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	static char buf[4];
	static int16_t x, y, dur, newval;
	static uint8_t rpmx = 1, rpmy = 1; // 1=x10 2=x100
	static int icon_play = CleO.LoadIcon("@Icons/m48.ftico", ICON_PLAY_CIRCLE_FILLED);
	static int icon_8 = CleO.LoadIcon("@Icons/m48.ftico", ICON_FILTER_8);
	static int icon_6 = CleO.LoadIcon("@Icons/m48.ftico", ICON_FILTER_6);
	static int icon_4 = CleO.LoadIcon("@Icons/m48.ftico", ICON_FILTER_4);
	static int icon_loop = CleO.LoadIcon("@Icons/m48.ftico", ICON_REPLAY);
	static int icon_stop = CleO.LoadIcon("@Icons/m48.ftico", ICON_STOP);
	static int icon_pause = CleO.LoadIcon("@Icons/m48.ftico", ICON_PAUSE);
	static int icon_barras = CleO.LoadIcon("@Icons/m48.ftico", ICON_FORMAT_LIST_BULLETED);
	//int icon_left = CleO.LoadIcon("@Icons/m48.ftico", ICON_KEYBOARD_ARROW_LEFT);
	//int icon_right = CleO.LoadIcon("@Icons/m48.ftico", ICON_KEYBOARD_ARROW_RIGHT);
	static int icon_remove_circle = CleO.LoadIcon("@Icons/m48.ftico", ICON_REMOVE_CIRCLE);
	static int icon_add_circle = CleO.LoadIcon("@Icons/m48.ftico", ICON_ADD_CIRCLE);

	//int16_t font = CleO.LoadFont("@Fonts/DSEG7ClassicMini-BoldItalic.ftfont");
	static boolean key = false, pause, teclado = false;
	static textfield AlphaNumeric = { 16, "" };

	for (;;)
	{
		CleO.Start();

		//CleO.SetBackgroundGradient(0, 0, 800, 480, DARK_BLUE, DARK_GREEN);
		CleO.SetBackgroundcolor(BLACK);

		if (key) {

			switch (rpmy) {
			case 0: CleO.StringExt(FONT_SANS_3, 150, 220, WHITE, MM, 0, 0, "x1");
				break;
			case 1: CleO.StringExt(FONT_SANS_3, 150, 220, WHITE, MM, 0, 0, "x10");
				break;
			case 2: CleO.StringExt(FONT_SANS_3, 150, 220, WHITE, MM, 0, 0, "x100");
				break;
			case 3: CleO.StringExt(FONT_SANS_3, 150, 220, WHITE, MM, 0, 0, "x1000");
				break;
			default: rpmy = 0;
			}

			CleO.StringExt(FONT_SANS_7, 650, 140, GOLD, MM, 0, 0, print_number(60000 / RPM));
			CleO.StringExt(FONT_SANS_3, 650, 80, LIGHT_GREEN, MM, 0, 0, "Shaft Period (mseg)");

			if (vueltas > 32768) vueltas = 0;
			CleO.StringExt(FONT_SANS_7, 390, 140, GOLD, MM, 0, 0, print_number(vueltas));
			CleO.StringExt(FONT_SANS_3, 390, 80, LIGHT_GREEN, MM, 0, 0, "Shaft Rot.(rev)");

			CleO.Tag(95);
			CleO.StringExt(FONT_SANS_7, 150, 140, GOLD, MM, 0, 0, print_number(tiempo_inyeccion_us));
			CleO.StringExt(FONT_SANS_3, 150, 80, LIGHT_GREEN, MM, 0, 0, "Injection Time (uS)");

			CleO.StringExt(FONT_SANS_7, 150, 340, GOLD, MM, 0, 0, print_number(cursor + 1));
			CleO.StringExt(FONT_SANS_3, 150, 280, LIGHT_GREEN, MM, 0, 0, "Sequence (#)");

			CleO.Tag(102); // Iconos 4, 6 8
			CleO.Bitmap(icon_4, 443, 400);
			CleO.Tag(103);
			CleO.Bitmap(icon_6, 511, 400);
			CleO.Tag(104);
			CleO.Bitmap(icon_8, 580, 400);

			CleO.Tag(96); // cursores Tiempo inyeccion
			CleO.Bitmap(icon_remove_circle, 50, 200);
			CleO.Tag(97);
			CleO.Bitmap(icon_add_circle, 205, 200);

			// - y + de O2 Auto
			CleO.Tag(98);
			CleO.Bitmap(icon_remove_circle, 50, 400);
			CleO.Tag(99);
			CleO.Bitmap(icon_add_circle, 205, 400);

			switch (estado)
			{
			case cuatro_cilindros: CleO.StringExt(FONT_SANS_5, 390, 340, GOLD, MM, 0, 0, motor_4[cursor]);
				break;
			case seis_cilindros: CleO.StringExt(FONT_SANS_5, 390, 340, GOLD, MM, 0, 0, motor_6[cursor]);
				break;
			case ocho_cilindros: CleO.StringExt(FONT_SANS_5, 390, 340, GOLD, MM, 0, 0, motor_8[cursor]);
				break;
			default:
				break;
			}
		}
		else
		{
			CleO.Tag(1);
			CleO.StringExt(FONT_SANS_7, 150, 140, GOLD, MM, 0, 0, read_Ax(A0));
			CleO.StringExt(FONT_SANS_3, 150, 80, LIGHT_GREEN, MM, 0, 0, "EGR (Volts)");

			CleO.Tag(2);
			CleO.StringExt(FONT_SANS_7, 390, 140, GOLD, MM, 0, 0, read_Ax(A1));
			CleO.StringExt(FONT_SANS_3, 390, 80, LIGHT_GREEN, MM, 0, 0, "MAP (Volts)");


			CleO.Tag(3);
			CleO.StringExt(FONT_SANS_7, 650, 140, GOLD, MM, 0, 0, read_Ax(A2));
			CleO.StringExt(FONT_SANS_3, 650, 80, LIGHT_GREEN, MM, 0, 0, "MAF (Volts)");

			CleO.Tag(4);
			CleO.StringExt(FONT_SANS_7, 150, 310, GOLD, MM, 0, 0, read_Ax(A3));
			CleO.StringExt(FONT_SANS_3, 150, 250, LIGHT_GREEN, MM, 0, 0, "O2 (Volts)");

			//  Frecuencia O2
			CleO.Tag(92);
			CleO.StringExt(FONT_SANS_5, 152, 420, (O2_ON) ? GOLD : RED, MM, 0, 0, print_number(frecuencia_O2));
			CleO.StringExt(FONT_SANS_3, 160, 370, LIGHT_GREEN, MM, 0, 0, "O2 Auto (Hz)");

			CleO.Tag(5);
			CleO.StringExt(FONT_SANS_7, 390, 310, GOLD, MM, 0, 0, read_Ax(A4));
			CleO.StringExt(FONT_SANS_3, 390, 250, LIGHT_GREEN, MM, 0, 0, "TPS (Volts)");

			// - y + de O2 Auto
			CleO.Tag(93);
			CleO.Bitmap(icon_remove_circle, 50, 400);
			CleO.Tag(94);
			CleO.Bitmap(icon_add_circle, 205, 400);
		}

		switch (rpmx) {
		case 0: CleO.StringExt(FONT_SANS_3, 650, 250, LIGHT_GREEN, MM, 0, 0, "RPM (x1)");
			break;
		case 1: CleO.StringExt(FONT_SANS_3, 650, 250, LIGHT_GREEN, MM, 0, 0, "RPM (x10)");
			break;
		case 2: CleO.StringExt(FONT_SANS_3, 650, 250, LIGHT_GREEN, MM, 0, 0, "RPM (x100)");
			break;
		case 3: CleO.StringExt(FONT_SANS_3, 650, 250, LIGHT_GREEN, MM, 0, 0, "RPM (x1000)");
			break;
		default: rpmx = 0;
		}

		sprintf(buf, "%d", RPM);
		CleO.Tag(91);
		CleO.StringExt(FONT_SANS_7, 650, 310, GOLD, MM, 0, 0, buf);

		// Display and tag play icon
		CleO.Tag(100);
		CleO.Bitmap((key) ? icon_loop : icon_play, 650, 400);
		CleO.Tag(101);
		CleO.Bitmap((key) ? icon_pause : icon_barras, 350, 400);

		// Engine ON
		if (motor_on) {
			CleO.StringExt(FONT_SANS_2, 10, SCREEN_HEIGHT - 10, RED, BL, 0, 0, "Engine ON  CKPS: 1P/2T");
		}

		/* Display completed screen frame */
		CleO.Show();


		RPM = (value1 >> 2)* ((rpmx == 0) + (rpmx == 1) * 10 + (rpmx == 2) * 100 + (rpmx == 3) * 1000);
		if (RPM != RPM_OLD) Timer3.setPeriod((16500 / RPM) * 6);
		RPM_OLD = RPM;

		CleO.TouchCoordinates(x, y, dur, tag);
		switch (tag) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			clear_leds();
			vTaskResume(xHandle_3);  // activo voltímetro
			vTaskSuspend(xHandle_2); // paro menús
			break;

		case 91: // RPM
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			rpmx++; if (rpmx == 4) rpmx = 0;
			delay(350);
			break;

		case 92: // - O2
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			O2_ON = !O2_ON;
			if (O2_ON) Timer1.attachInterrupt(timer_O2); else { Timer1.detachInterrupt(); digitalWrite(pulso_O2, 0); }
			//if (rpmy == 4) rpmy = 0;
			delay(350);
			break;

		case 93: // - O2
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			frecuencia_O2--;
			Timer1.setPeriod(500000 / frecuencia_O2);
			delay(350);
			break;

		case 94: // + o2
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			frecuencia_O2++;
			Timer1.setPeriod(500000 / frecuencia_O2);
			//if (rpmy == 4) rpmy = 0;
			delay(350);
			break;

		case 95: // x10 injection time
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			rpmy++;
			if (rpmy == 4) rpmy = 0;
			delay(350);
			break;

		case 96: // cursor Izq. Tiempo inyección
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			if (tiempo_inyeccion_us != 0) tiempo_inyeccion_us -= ((rpmy == 0) + (rpmy == 1) * 10 + (rpmy == 2) * 100 + (rpmy == 3) * 1000);
			delay(350);
			break;

		case 97: // cursor Der. Tiempo inyección
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			tiempo_inyeccion_us += ((rpmy == 0) + (rpmy == 1) * 10 + (rpmy == 2) * 100 + (rpmy == 3) * 1000);
			delay(350);
			break;

		case 98: // cursor Izq.
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			delay(350);
			if (cursor != 0) cursor--;
			break;

		case 99: // cursor Derecho
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			cursor++;
			delay(350);
			if ((estado == cuatro_cilindros) & (cursor) > 1) cursor = 0;
			if ((estado == seis_cilindros) & (cursor) > 4) cursor = 0;
			if ((estado == ocho_cilindros) & (cursor) > 1) cursor = 0;
			break;

		case 100:   // Play
			(key) ? key = false : key = true;
			delay(300);
			CleO.SetSound(PIPS(10), 60, 30, 0); // volver
			CleO.SoundPlay(1);
			if (key) {
				menu();
			}
			else {
				clear_leds();
			}
			break;

		case 101: // Pause - Multiplot
			CleO.SetSound(PIPS(10), 60, 30, 0); // volver
			CleO.SoundPlay(1);
			if (key) {
				pause = !pause;
				if (pause) {
					Timer3.detachInterrupt(); // attach the service routine here
					motor_on = false;
					vTaskDelay(150 / portTICK_PERIOD_MS);
				}
				else menu();
			}
			else {
				vTaskResume(xHandle_4);  // activo multiplot
				vTaskSuspend(xHandle_2); // paro menús}
			}
			break;

		case 102: // 4cil
			CleO.SetSound(PIPS(10), 60, 30, 0); // volver
			CleO.SoundPlay(1);
			if (key) {
				// Pines salida
				clear_leds();
				estado = cuatro_cilindros;
				cursor = 0;
				Timer3.attachInterrupt(timer_4cil); // attach the service routine here
			}
			else {
				Timer3.detachInterrupt(); // attach the service routine here
			}
			break;

		case 103: // 6cil
			CleO.SetSound(PIPS(10), 60, 30, 0); // volver
			CleO.SoundPlay(1);
			if (key) {
				clear_leds();
				estado = seis_cilindros;
				cursor = 0;
				Timer3.attachInterrupt(timer_6cil); // attach the service routine here
			}
			else {
				Timer3.detachInterrupt();
			}
			break;

		case 104: // 8cil
			CleO.SetSound(PIPS(10), 60, 30, 0); // volver
			CleO.SoundPlay(1);
			if (key) {
				clear_leds();
				estado = ocho_cilindros;
				cursor = 0;
				Timer3.attachInterrupt(timer_8cil); // attach the service routine here
			}
			else {
				Timer3.detachInterrupt();
			}
			break;
		default:
			break;
		}

		if (!digitalRead(encoderSwitchPin1)) {
			delay(2000);
			vTaskResume(xHandle_1);  // activo salvapantallas
			vTaskSuspend(xHandle_2); // paro menús}
		}

		/*
		1.TL - Top Left
		2.TM - Top Middle
		3.TR - Top Right
		4.ML - Middle Left
		5.MM - Middle Middle
		6.MR - Middle Right
		7.BL - Bottom Left
		8.BM - Bottom Middle
		9.BB - Bottom Bottom
		10.BR - Bottom Right*/

		vTaskDelayUntil(&xLastWakeTime, (100 / portTICK_PERIOD_MS));
	}
}

//////////////////////////////////////////////////////////////////////
//  voltimetros
//////////////////////////////////////////////////////////////////////
void vPeriodicTask3(void *pvParameters)
{
	TickType_t xLastWakeTime = xTaskGetTickCount();
	delay(1000);
	static int16_t voltmc, x, y, dur, newval;

	// sample analog pin A0
	static int angle;
	static const int cx = 400;              // X center
	static const int cy = 480;              // Y Center
	static const int r = 365;               // Gauge radius
	static int lineLength, cuenta;
	static char* sennales[] = { "---", "EGR", "MAP", "MAF","02","TPS" };
	static char buf[4];

	for (;;)
	{
		// sample analog pin Ax
		angle = gauge_angle(analogRead(tag - 1));

		/* Start building a screen frame */
		CleO.Start();

		CleO.SetBackgroundcolor(WHITE);

		CleO.LineColor(BLACK);

		/* Draw arcs with 30 lines */
		for (uint8_t i = 0; i <= 30; i++)
		{
			if (i % 6 != 0) {
				lineLength = 30;
				CleO.LineWidth(1);
			}
			else {
				lineLength = 60;
				CleO.LineWidth(2);
			}

			CleO.LineR1R2(cx, cy, r + lineLength, r, 135 + i * 90.00 / 30);
		}

		/* Draw V logo with two lines below it */
		//CleO.StringExt(FONT_HUGE, cx, cy / 2, BLACK, MM, 0, 0, sennales[tag]);
		CleO.StringExt(FONT_HUGE, 100, 400, BLACK, BL, 0, 0, sennales[tag]);
		CleO.StringExt(FONT_HUGE, cx, cy / 2, BLACK, MM, 0, 0, read_Ax(A0 + tag - 1));
		CleO.LineExt(cx - 28, cy / 2 + 60, 60, BLACK, 270, 0);
		CleO.LineExt(cx - 28, cy / 2 + 75, 12, BLACK, 270, 0);
		CleO.LineExt(cx - 5, cy / 2 + 75, 12, BLACK, 270, 0);
		CleO.LineExt(cx + 20, cy / 2 + 75, 12, BLACK, 270, 0);

		// escribo los números
		for (uint8_t i = 0; i < 6; i++) {
			itoa(i, buf, 10);
			CleO.StringExt(FONT_SANS_6, cx, cy, BLACK, MM, gauge_angle(i * 1023 / 5), r + 100, buf);
		}

		// Red needle
		CleO.NeedleWidth(12);
		CleO.NeedleColor(ORANGERED);
		CleO.NeedleXY(cx, cy, r + 25, angle);

		CleO.Tag(6);
		CleO.StringExt(FONT_SANS_6, 500, 400, BLACK, BL, 0, 0, "<-BACK");

		CleO.Show();

		CleO.TouchCoordinates(x, y, dur, tag2);
		if ((tag2 == 6) | (cuenta++ > 100))
		{
			CleO.SetSound(PIPS(10), 60, 30, 0);
			CleO.SoundPlay(1);
			cuenta = 0;
			vTaskResume(xHandle_2);  // setup menus
			vTaskSuspend(xHandle_3); // stop voltmeter
		}

		vTaskDelayUntil(&xLastWakeTime, (50 / portTICK_PERIOD_MS));
	}
}

//////////////////////////////////////////////////////////////////////
//  Motor
//////////////////////////////////////////////////////////////////////

void addsamples(float s0, float s1, float s2) {
	for (int i = 0; i < 31; i++) {
		samples[0][i] = samples[0][i + 1];
		samples[1][i] = samples[1][i + 1];
		samples[2][i] = samples[2][i + 1];
	}
	samples[0][31] = s0;
	samples[1][31] = s1;
	samples[2][31] = s2;
	if (nsamples < 32)
		nsamples += 1;
}

//////////////////////////////////////////////////////////////////////
//  Triple gráfica
//////////////////////////////////////////////////////////////////////

void vPeriodicTask4(void *pvParameters)
{
	delay(1000);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const static uint32_t colors[3] = { DEEP_SKY_BLUE, GOLD, WHITE };
	const static char text[][20] = { "O2" ,"TPS","RPM","EGR", "MAP", "MAF" };
	static int16_t x, y, dur, newval;
	static bool estatus[] = { true,true,true };
	int icon_loop = CleO.LoadIcon("@Icons/m48.ftico", ICON_REPLAY);
	static uint16_t cuentas;

	for (;;)
	{
		addsamples((estatus[0]) ? analogRead(A0) : analogRead(A3), (estatus[1]) ? analogRead(A1) : analogRead(A4), (estatus[2]) ? analogRead(A2) : RPM);
		cuentas++;

		/* Start drawing the screen shot */
		CleO.Start();

		float mn, mx, scale;
		CleO.LineWidth(2);

		float channel_display_width = 740;
		float channel_display_height = 120;

		/* For each channel */
		for (int chan = 0; chan < 3; chan++) {
			int ybase = 150 + 150 * chan;

			/* Draw a rectangle section in which reading will be drawn */
			CleO.Tag(chan + 10);
			CleO.RectangleExt(0, ybase, channel_display_width, channel_display_height, 0x303030, BL, 0, 0);
			CleO.StringExt(FONT_SANS_2, channel_display_width, ybase - channel_display_height, colors[chan], TR, 0, 0, text[chan + 3 * estatus[chan]]);

			mn = mx = samples[chan][31];
			float *s = samples[chan];

			/* from all samples in a channel, get min and max */
			for (int i = (32 - nsamples); i < 32; i++) {
				mn = min(mn, s[i]);
				mx = max(mx, s[i]);
			}

			/* Get the scaling value */
			if (mx != mn)
				scale = 90 / (mx - mn);
			else
				scale = 0;

			/* Select different color for each channel */
			CleO.LineColor(colors[chan]);
			CleO.StringExt(FONT_SANS_2, 10, SCREEN_HEIGHT, RED, BL, 0, 0, "Samples:");
			CleO.StringExt(FONT_SANS_2, 100, SCREEN_HEIGHT, RED, BL, 0, 0, print_number(cuentas));

			/* Connect all samples for a channel with a line */
			for (int i = (32 - nsamples); i < 31; i++) {
				float x0 = i * (channel_display_width / 31);
				float x1 = (i + 1) * (channel_display_width / 31);
				float y0 = ybase - scale * (s[i] - mn);
				float y1 = ybase - scale * (s[i + 1] - mn);
				CleO.Line(x0, y0, x1, y1);
			}

			/* Get the y coordinate for the last sample and use it for drawing reading
			as text */
			char val[9];
			sprintf(val, "%d", int(s[31]));
			float y = ybase - scale * (s[31] - mn);
			CleO.StringExt(FONT_SANS_3, channel_display_width + 8, y, colors[chan], BL, 0, 0, val);
		}

		CleO.Tag(13);
		CleO.Bitmap(icon_loop, 750, 435);

		/* Show the screen shot */
		CleO.Show();

		CleO.TouchCoordinates(x, y, dur, tag);
		switch (tag) {
		case 10:
			estatus[0] = estatus[0] ^ 1;
			break;
		case 11:
			estatus[1] = estatus[1] ^ 1;
			break;
		case 12:
			estatus[2] = estatus[2] ^ 1;
			break;
		case 13:
			vTaskResume(xHandle_2);  // paro multiplot
			vTaskSuspend(xHandle_4); // activo menús}
			break;
		default:
			break;
		}

		vTaskDelay(10 / portTICK_PERIOD_MS);
		//vTaskDelayUntil(&xLastWakeTime, (100 / portTICK_PERIOD_MS));
	}
}

//////////////////////////////////////////////////////////////////////
//  O2
//////////////////////////////////////////////////////////////////////
void vPeriodicTask5(void *pvParameters)
{
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for (;;)
	{
		digitalWrite(pulso_O2, digitalRead(pulso_O2) ^ 1);
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}

	//vTaskDelayUntil(&xLastWakeTime, (500 / portTICK_PERIOD_MS));
}


void updateEncoder1() {

	static uint8_t MSB, LSB, encoded, sum;

	MSB = digitalRead(encoderPin2); //MSB = most significant bit
	LSB = digitalRead(encoderPin3); //LSB = least significant bit
	encoded = (MSB << 1) | LSB; //converting the 2 pin value to single number
	sum = (lastEncoded1 << 2) | encoded; //adding it to the previous encoded value

	if ((sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) & (value1 != 0)) value1--;
	else if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) value1++;

	lastEncoded1 = encoded; //store this value for next time
}

void loop() { }



