//Tasarım Proje
#include "stdafx.h"
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include <ctype.h>

using namespace cv;//opencv isim uzayı
using namespace std;


#define cam 0  //kamera tanımlandı (webcam için 0, harici kameralar için 1,2,3 vb.)

// değişkenler oluşturuldu
typedef enum tracking {
	takip_yok,  // herhangi bir işlem yapılmadığında ve klavyeden c tuşuna basıldığında
	secim,  // takip edilecek nesneyi belirten dikdörtgen kullanıcı tarafından oluşturulur ama camshift henüz başlamamıştır
	takip  // Nesne hedef haline gelir ve takibin devamlılığı sağlanır
} tracking;

bool kare_sec = false;   // ekranda hedef seçilir iken true, diğer durumlarda false olacak bir boolean değişken oluşturuldu.
tracking durum = takip_yok;  // başlangıç 
bool hist_goster = true;  // histogram gösterip göstermemek için boolean bir değişken tanımlandı(klavyeden h tuşu ile histogram penceresi açılacak)
bool paused = false;   //kameradan gelen akışın durdurulması 
Point origin;
Rect hedef_sec;
int vmin = 10, vmax = 256, smin = 30;//hsv renk uzayına ait min-max parlaklık ve min doygunluk değerleri atandı.

static void onMouse(int event, int x, int y, int, void*);//pencere üzerinde gerçekleşecek mouse dikdörtgen seçim işlemi
static void help();

Mat image, imageBackProjection;
const char* keys = { "{1|  | 0 | camera number}" };

int main(int argc, const char** argv)
{
	help();  // konsola kullanımı yazdır

	VideoCapture cap;
	Rect dinamikTakipPenceresi;  // kullanıcının seçtiği takip penceresi

	int histogram_sutun = 16;  // histogramdaki sütun sayısı 
	float histogram_aralik[] = { 0,180 };  // 0 ile 180 arasındaki piksel değerlerini kullanacağız
	const float* phistogram_aralik = histogram_aralik;

	CommandLineParser parser(argc, argv, keys);
	int camNum = parser.get<int>("1");

	cap.open(cam);  // kamera aç

	if (!cap.isOpened())//kamera açılmaz ise kontrol
	{
		help();
		cout << "Kamera acilmadi \n";

		return -1;
	}

	double fps = cap.get(CV_CAP_PROP_FPS);
	cout << "FPS: " << fps << endl;
	double dt = 1. / fps;

	// gerekli pencereler oluşturuldu
	namedWindow("Histogram", 0);
	namedWindow("Nesne Takip", CV_WINDOW_AUTOSIZE);
	namedWindow("Mask", CV_WINDOW_AUTOSIZE);
	namedWindow("Back Projection", CV_WINDOW_AUTOSIZE);

	// pencerelerin başlangıç pozisyonları
	cvMoveWindow("Nesne Takip", 0, 30);
	cvMoveWindow("Histogram", 300, 30);
	cvMoveWindow("Back Projection", 600, 30);
	cvMoveWindow("Mask", 600, 500);

	setMouseCallback("Nesne Takip", onMouse, 0);//nesne takip penceresi içerisinde mouse ile seçim için
												// parlaklık ve doygunluk değerlerinin ayarlamaları için trackbar oluşturuldu ve nesne takip penceresine yerleştirildi.
	createTrackbar("Vmin", "Nesne Takip", &vmin, 256, 0);
	createTrackbar("Vmax", "Nesne Takip", &vmax, 256, 0);
	createTrackbar("Smin", "Nesne Takip", &smin, 256, 0);
	//////////////////////////////////////////////kalman filtresi

	KalmanFilter KF(4, 2, 0);
	Mat state(4, 1, CV_32F);
	Mat processNoise(4, 1, CV_32F);
	Mat measurement = Mat::zeros(2, 1, CV_32F);

	//setIdentity(KF.transitionMatrix);
	//KF.transitionMatrix.at<double>(0, 2) = dt;
	//KF.transitionMatrix(1, 3) = dt;
	KF.transitionMatrix = (Mat_<float>(4, 4) << 1, 0, dt, 0, 0, 1, 0, dt, 0, 0, 1, 0, 0, 0, 0, 1);
	setIdentity(KF.measurementMatrix);
	setIdentity(KF.processNoiseCov, Scalar::all(1e-3));
	setIdentity(KF.measurementNoiseCov, Scalar::all(1e-3));
	setIdentity(KF.errorCovPost, Scalar::all(1));

	cout << "transitionMatrix\n" << KF.transitionMatrix << endl;
	cout << "measurementMatrix\n" << KF.measurementMatrix << endl;
	cout << "processNoiseCov\n" << KF.processNoiseCov << endl;
	cout << "measurementNoiseCov\n" << KF.measurementNoiseCov << endl;

	////////////////////////////////////////////////kalman filtresi



	Mat frame, hsv, hue, mask, hist, histimg = Mat::zeros(200, 320, CV_8UC3), backproj;

	for (;;)  // akış oluşuturabilmek için sınırsız döngü
	{
		if (!paused)  // frame frame aktarım yapılıyor. arka arkaya gelen framelerden canlı akış sağlanacak
		{
			cap >> frame;
			if (frame.empty())//okunacak frame kalmaz ise
				break;
		}

		frame.copyTo(image);  // her frame'i image değişkenine kopyala

		if (!paused)
		{
			cvtColor(image, hsv, COLOR_BGR2HSV);  // image değişkenini BGR renk uzayından HSV renk uzayına çevirdik 

			if (durum != takip_yok)
			{
				// Scalar nesnesi, bir pikselin değerlerini depolamasına izin verir

				//bu kısım önemli//////////////////////////////////////////////////////////////////////////////////////////////

				inRange(hsv, Scalar(0, smin, MIN(vmin, vmax)), Scalar(180, 256, MAX(vmin, vmax)), mask);
				//seçilen renkteki piksellerin maskesini bulmak için inRange fonksiyonu kullanıldı ve maskeyle görüntü üzerinde seçilen pikseller elde edildi

				// HSV maskesi aslında şu şekilde oldu:
				// 0 < H < 180
				// smin < S < 256
				// MIN(vmin,vmax) < V < MAX(vmin, vmax)

				imshow("Mask", mask);//elde edilen maskenin olusturulan pencereye aktarımı

				int ch[] = { 0, 0 };

				hue.create(hsv.size(), hsv.depth());
				// matirisin renk tonu değişkenini (hue) hsv'ye çevrilmiş frame ile aynı boyuta ayarladık

				mixChannels(&hsv, 1, &hue, 1, ch, 1);  //mixChannels Belirtilen kanalları giriş dizilerinden belirtilen çıkış dizileri kanallarına kopyalar.
													   // hsv'nin 0. kanalı (renk kanalı) hue'nun 0. kanalı oldu
													   //dolayısıyla hue matrisinin sadece 1 kanalı var. Bu kanalda hsv matrisinden gelen renk kanalı
													   //hsv-->hue, saturation, value hue->renk özü, saturation->doygunluk, value->parlaklık

				if (durum == secim)  //kullanıcı nesne seçimi yapacak dolayısıyla takip başlamalı
				{

					// iki tane dinamik pencere oluşturulacağı için 
					Mat bolge(hue, hedef_sec);
					Mat maskebolge(mask, hedef_sec);

					// histogram hesaplaması renkten (hue) ile yapılır
					calcHist(&bolge, 1, 0, maskebolge, hist, 1, &histogram_sutun, &phistogram_aralik);

					// yardım alındı///////////// http://docs.opencv.org/doc/tutorials/imgproc/histograms/histogram_calculation/histogram_calculation.html

					// 0 ile 255 arasında 
					normalize(hist, hist, 0, 255, CV_MINMAX);

					dinamikTakipPenceresi = hedef_sec;
					durum = takip;

					KF.statePost.at<float>(0) = hedef_sec.x + hedef_sec.width / 2.;
					KF.statePost.at<float>(1) = hedef_sec.y + hedef_sec.height / 2.;
					KF.statePost.at<float>(2) = 0.;
					KF.statePost.at<float>(3) = 0.;

					cout << "State post: \n" << KF.statePost << endl;

					histimg = Scalar::all(0);  // histogramı sıfırla
					int columnWidth = histimg.cols / histogram_sutun;
					// sütun genişliği = histogram genişliği / sütun sayısı

					Mat buf(1, histogram_sutun, CV_8UC3);
					// CV_8UC3 = 8 bit, unsigned char, 3 kanal

					// histogram için renk prosesi
					for (int i = 0; i < histogram_sutun; i++)
						buf.at<Vec3b>(i) = Vec3b(saturate_cast<uchar>(i*180. / histogram_sutun), 255, 255);
					cvtColor(buf, buf, CV_HSV2BGR);

					//histogram yazdırma
					for (int i = 0; i < histogram_sutun; i++)
					{
						int val = saturate_cast<int>(hist.at<float>(i)*histimg.rows / 255);
						rectangle(histimg, Point(i*columnWidth, histimg.rows),
							Point((i + 1)*columnWidth, histimg.rows - val),
							Scalar(buf.at<Vec3b>(i)), -1, 8);
					}
				}

				calcBackProject(&hue, 1, 0, hist, backproj, &phistogram_aralik);
				// hist değişkeni histogram ve backproj değişkeni sonuç imagedir.

				// Back Projection kısaca:
				//belirli bir görüntünün piksellerinin histogram modelindeki piksel dağılımına ne kadar iyi uyduğunu kaydetmenin bir yoludur.
				// görüntü histogramdaki piksel dağıılmına uyuyor
				// kaynak/////////////////// http://docs.opencv.org/doc/tutorials/imgproc/histograms/back_projection/back_projection.html

				backproj = backproj & mask; // '&' lojik AND manasına gelir (backproj AND mask)
											// hsv değerleri ile kameradan gelen görüntüler ile maskeyi oluşturmuştuk 
											//backproj değişkeni hedefin renginin ton değerine bağlıdır.

				RotatedRect trackBox = CamShift(backproj, dinamikTakipPenceresi,
					TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1));
				// probImage –> histogramın back projection'ı
				// dinamikTakipPenceresi -> arama penceresi

				// CamShift algoritması nesnenin konumunu, boyutunu ve yönünü döndürür(return)
				// izleme penceresinin sonraki konumu RotatedRect::boundingRect() ile elde edilebilir

				if (dinamikTakipPenceresi.area() <= 1)
				{
					int cols = backproj.cols, rows = backproj.rows;
					int r = (MIN(cols, rows) + 5) / 6;
					dinamikTakipPenceresi = Rect(dinamikTakipPenceresi.x - r, dinamikTakipPenceresi.y - r, dinamikTakipPenceresi.x + r, dinamikTakipPenceresi.y + r)
						& Rect(0, 0, cols, rows);
				}

				cvtColor(backproj, imageBackProjection, COLOR_GRAY2BGR);
				// backproj'un renk formatını değiştirdik
				// piksel ne kadar açıksa, izlenen değerlerle o kadar çok eşleşir////////////

				ellipse(image, trackBox, Scalar(0, 0, 255), 3, CV_AA); // nesne takip penceresine elipsi yerleştirdik
				circle(image, trackBox.center, 3, Scalar(0, 0, 255), 3);
				ellipse(imageBackProjection, trackBox, Scalar(0, 0, 255), 3, CV_AA); //imageBackProjection penceresine elipsi yerleştirdik
				circle(imageBackProjection, trackBox.center, 3, Scalar(0, 0, 255), 3);

				// Kalman filtresi çalıştı ve Mavi circle oluştu
				Mat prediction = KF.predict();
				measurement.at<float>(0) = trackBox.center.x;
				measurement.at<float>(1) = trackBox.center.y;
				KF.correct(measurement);

				Point estimate(KF.statePost.at<float>(0), KF.statePost.at<float>(1));
				circle(image, estimate, 3, Scalar(255, 0, 0), -1);
				//////////////////////////kalman

				imshow("Back Projection", imageBackProjection);
			}
		}
		else if (durum == secim)  // 
			paused = false;

		// kare==true => kullanıcı kareyi secme işlemini bitirmedi yani sol butondan henüz elini çekmedi
		if (kare_sec == true && hedef_sec.width > 0 && hedef_sec.height > 0)
		{
			Mat roi(image, hedef_sec);
			bitwise_not(roi, roi);
		}

		imshow("Nesne Takip", image);
		imshow("Histogram", histimg);

		char c = (char)waitKey(10);
		if (c == 27)  // 27 ascii karşılığı escye tekabül eder
			break;
		switch (c)
		{
		case 't':   //takibi bitir
			durum = takip_yok;
			histimg = Scalar::all(0);
			break;
		case 'h':   // histogram penceresi
			hist_goster = !hist_goster;
			if (!hist_goster)
				destroyWindow("Histogram");
			else
				namedWindow("Histogram", 1);
			break;
		case 'p':
			paused = !paused;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void onMouse(int event, int x, int y, int, void*)//fare ile hedefin seçilme işlemleri
{
	if (kare_sec)
	{
		hedef_sec.x = MIN(x, origin.x);
		hedef_sec.y = MIN(y, origin.y);
		hedef_sec.width = std::abs(x - origin.x);
		hedef_sec.height = std::abs(y - origin.y);

		hedef_sec &= Rect(0, 0, image.cols, image.rows);
	}

	switch (event)
	{
	case CV_EVENT_LBUTTONDOWN:  // mouse'un sol tuşuna basıldığında
		origin = Point(x, y);
		hedef_sec = Rect(x, y, 0, 0);
		kare_sec = true;
		break;
	case CV_EVENT_LBUTTONUP:  // sol buton bırakıldığında
		kare_sec = false;
		if (hedef_sec.width > 0 && hedef_sec.height > 0)
			durum = secim;
		break;
	}
}

static void help()
{
	cout << "TASARIM PROJE\n Mustafa YILDIZ\n Mucahid KAYA\n";

	cout << "\n\n\n"
		"\t ESC - cikis\n"
		"\t t - takibi durdur\n"
		"\t h - histogram ac kapa \n"
		"\t p - akisi durdur\n"
		"mouse ile hedef secimi yapiniz\n";
}
//son kisim