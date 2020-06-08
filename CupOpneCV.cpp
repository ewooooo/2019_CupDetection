#define _CRT_SECURE_NO_WARNINGS

#define SerialOn 0
#define debug 0

#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>

#if SerialOn == 1
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#endif // SerialOn

int fd; //Serial 통신 장치명(번호)

using namespace std;
using namespace cv;

void videoWriteSet(Size size);	//영상파일 출력용
VideoWriter writer;				//영상파일 출력용

void findedge(Mat inImg, Size size);	//윤곽생성 및 조건에 대한 물체인식

double TemplateProcess(Rect rect, Mat resultImage, Mat tImage, double value);	//종이컵 인식

int centerX(Rect inRect);	//물체 중심 x좌표


int SerialSet();			//Serial 통신 초기 세팅
int SerialWrite(char *buf);	//Serial 통신 Tx 보내기

void addzero(char * strHex, int len);	//문자열 앞에 0 붙이는 함수

int testCount = 0;			//frame 하나당 카운트
int setFrameCount = 50;		//총 10프레임이 될때까지 카운트
double testHighValue = 1;	//탐색과정에서 낮은 탐색값 저장. 낮을수록 좋음 0~1;

int status = 0;		//모터 및 아두이노 제어상태  0:못찾음(default) 1:샘플링단계 2:추적중 3: 추적하다잃어버림 

Mat img_input, cupImage, buf_img;	//입력이미지, 컵 이미지, 버퍼이미지
Size camSizein;	//입력 영상 size

Mat plotImg;	//디버깅을 위한 plot이미지
int notFindingCount = 0;	//물체를 잃어버렸을때 카운팅하여 리셋하기 위한 변수

int main() {
	SerialSet();
	cupImage = imread("./4.png", 1);	//종이컵 이미지 
	cv::VideoCapture cap(0);
	if (!cap.isOpened()) {
		cerr << "에러 - 카메라를 열 수 없습니다.\n";
		return -1;
	}
	cap.set(3, 1920);		//영상 크기 수동조정
	cap.set(4, 1080);		//영상 크기 수동조정
	cap.set(CAP_PROP_AUTOFOCUS, 1);		//AF 수동조정 활성화
	//https://docs.opencv.org/3.3.1/d4/d15/group__videoio__flags__base.html#ggaeb8dd9c89c10a5c63c139bf7c4f5704dad937a854bd8d1ca73edfb3570e799aa3

	//웹캠 사이즈 받기
	camSizein = Size((int)cap.get(CAP_PROP_FRAME_WIDTH), (int)cap.get(CAP_PROP_FRAME_HEIGHT));

	int wSize = camSizein.width;
	int hSize = camSizein.height;
	Size size = Size(wSize, hSize);

	//Size size2 = Size(wSize*2, hSize*2);
	//videoWriteSet(size2);

	while (1)
	{
		// 카메라로부터 캡쳐한 영상을 frame에 저장합니다.
		cap.read(img_input);
		if (img_input.empty()) {
			cerr << "빈 영상이 캡쳐되었습니다.\n";
			break;
		}

		plotImg = img_input.clone();		//디버깅을 위한 복제


		//관심영역 만들기
		//Rect rectROI((camSizein.width -wSize)/2, (camSizein.height - hSize) / 2, wSize, hSize);	//x,y,w,h
		//Mat subImage = img_input(rectROI);
		/* //관심영영 확인
		rectangle(img_input,
			Point(rectROI.br().x - rectROI.width, rectROI.br().y - rectROI.height),
			rectROI.br(),
			Scalar(0, 255, 0), 5);
		*/

		//물체 발견 못했을 때 
		if (status == 0) {
			char bufo[] = "o";		
			SerialWrite(bufo);
#if debug == 1
			cout << "#default" << endl;
#endif
		}

		
		if (status == 2) {
			notFindingCount++;
			if (notFindingCount > 100000) {	// 추적중 물체 잃어버렸을 때 100000 카운트 넘으면 리셋 
				char buf[] = "t";
				SerialWrite(buf);
#if debug == 1
				cout << "#reset" << endl;
#endif
				buf_img.release();
				notFindingCount = 0;
				status = 0;
				char bufo[] = "o";
				SerialWrite(bufo);
#if debug == 1
				cout << "#default" << endl;
#endif

			}

		}

		findedge(img_input, camSizein);	//시작 지점 함수 윤곽인식


		if (status == 1 && testCount <= setFrameCount) {	//샘플링 스타트
			testCount++;
#if debug == 1
			cout << testCount << endl;
#endif
		}


		//writer.write(result); //영상 저장

		// ESC 키를 입력하면 루프가 종료됩니다.
		if (cv::waitKey(25) >= 0)
			break;
	}
	return 0;
}

void findedge(Mat inImg, Size size) {

	Mat bufImg = inImg.clone();
	//canny 연산
	Mat cannyi;
	Canny(inImg, cannyi, 30, 200);		//canny 알고리즘 적용
	imshow("canny", cannyi);

	//모폴로지 연상으로 구멍 매꾸기, 선 잇기
	Size userSize = Size(3, 3);
	dilate(cannyi, cannyi, getStructuringElement(MORPH_ELLIPSE, userSize));
	erode(cannyi, cannyi, getStructuringElement(MORPH_ELLIPSE, userSize));
	

	/*윤곽정보 생성*/
	vector<vector<Point> > contours;
	vector<Vec4i> hierarchy;
	findContours(cannyi, contours, hierarchy, CV_RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
	//CV_CHAIN_APPROX_NONE 외각선의 모든 점을 나열함
	//CV_CHAIN_APPROX_SIMPLE 수평, 수직이나 대각 외곽선의 끅점만 포함.


	//RNG rng(12345);
	//Mat drawing = Mat::zeros(inImg.size(), CV_8UC3);
	for (int idx = 0; idx < contours.size(); idx++) {

		//	Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
		//	drawContours(drawing, contours, idx, color, 2, 8, hierarchy, 0, Point());
		int setchbound = 20;//끝 변에서 부터 오는 라인들 ex 기둥, 벽
		int minScale = 30;// 최소 윤곽 크기
		Rect rect = boundingRect(contours[idx]);
		/*1차 조건 물체 크기 판별*/
		if (rect.height > minScale && rect.width > minScale && !(rect.width >= size.width - 5 && rect.height >= size.height - 5)) {
			/*2차 조건 물체 위치 판별(끝이랑 붙어있는지)*/
			if (rect.x < setchbound || (rect.x + rect.width) >size.width - setchbound || rect.y < setchbound || (rect.y + rect.height) > size.height - setchbound) {
				continue;	
			}
			/*3차 조건 검출된 물체 비율 판단*/
			if (rect.height / rect.width > 2.5 || (rect.height / rect.width) < 0.9) {
				continue;
			}

			rectangle(plotImg, Point(rect.br().x - rect.width, rect.br().y - rect.height), rect.br(), Scalar(0, 255, 0), 5);



			/*템플릿 매칭하려는 이미지를 가로세로 5씩 키워 안정성 향상*/
			int roiOffet = 5; //검출한 사각형 부터 옵셋을 주어 여유러운 검사 진행
			Rect testRect(rect.x - roiOffet, rect.y - roiOffet, rect.width + roiOffet * 2, rect.height + roiOffet * 2); //x,y,w,h
			Mat	resultImage = img_input(testRect); //비교할 이미지 




			if (!(testCount > setFrameCount && !(buf_img.empty()))) {	//첫 10프레임은 정지해서 계속 찍는다 and 샘플이미지가 안생겨도(컵 못찾으면)

				Mat tImage;
				resize(cupImage, tImage, Size(rect.width, rect.height), INTER_LINEAR);	//안정을 위해 리사이즈 
				double testValue = TemplateProcess(rect, resultImage, tImage, 0.2);	//템플릿 매칭 -> 샘플 컵 
				imshow("비교대상이미지", resultImage);
				imshow("비교할 컵 이미지", tImage);
				//double testValue = TemplateProcess(rect, resultImage, tImage,0.15);	//템플릿 매칭 -> 샘플 컵 

				if (testValue != 1) {
					if (testValue < testHighValue) {	//인식률 높은 이미지를 판별하기 위해
						status = 1;//샘플링 시작

						char buf[] = "s";
						SerialWrite(buf); //차량 정지 ==> 샘플링 중에는 차량을 정지한다.
#if debug == 1
						cout << "@@샘플링 시작" << endl;
#endif

						testHighValue = testValue;			//인식률 높은 이미지를 판별하기 위해
						buf_img = inImg(testRect).clone();	//버퍼이미지에 현재이미지 저장
						imshow("저장된 이미지", buf_img);
#if debug == 1
						cout << "저장완료" << endl;
#endif
					}
				}
			}
			else {

				if (abs(buf_img.cols - rect.width) > 200 || abs(buf_img.rows - rect.width) > 200) {	//샘플이랑 크기차이가 너무 나는건 걸름.
					continue;
				}
				Mat bufTimage;
				resize(buf_img, bufTimage, Size(rect.width, rect.height), INTER_LINEAR);	//안정을 위해 리사이즈
				imshow("비교대상이미지", resultImage);
				imshow("비교할 저장 이미지", bufTimage);
				double testValue2 = TemplateProcess(rect, resultImage, bufTimage, 0.15);	//템플릿 매칭 -> 이전 매칭 이미지

				if (testValue2 != 1) {
					rectangle(plotImg, Point(rect.br().x - rect.width, rect.br().y - rect.height), rect.br(), Scalar(0, 0, 255), 2);
					Mat tImage;
					resize(cupImage, tImage, Size(rect.width, rect.height), INTER_LINEAR);	//안정을 위해 리사이즈 
					double testValue = TemplateProcess(rect, resultImage, tImage, 0.3);	//템플릿 매칭 -> 샘플 컵 
					if (testValue != 1) {
						status = 2; //추적시작
						notFindingCount = 0; //찾으면 리셋;

						char buf[] = "m";
						SerialWrite(buf); //추적시작 움직여라(다음신호 주는건 오른쪽인지 왼쪽인지:아두이노는 다음신호를 받아야 움직인다.)
#if debug == 1
						cout << "@@추적 시작" << endl;
#endif

						buf_img = inImg(rect).clone(); //이전이미지를 버퍼 저장 (변화하는 환경 갱신)
						rectangle(plotImg, Point(rect.br().x - rect.width, rect.br().y - rect.height), rect.br(), Scalar(255, 0, 0), 2);
						imshow("Roitest", buf_img);
						imshow("setImg", plotImg);
#if debug == 1
						cout << "okok" << endl;
						cout << "center dist" << centerX(rect) - size.width / 2 << endl;
#endif
						if (abs(centerX(rect) - size.width / 2) < 100) {		//물체가 차량 중앙에 있는 지 판별 오차범위 100
							char buf[] = "c";
							SerialWrite(buf);
#if debug == 1
							cout << "#center" << endl;

							int dist = centerX(rect) - size.width / 2;
							char buf2[5];
							sprintf(buf2, "%d", abs(dist));
							addzero(buf2, 3);
							if (dist >= 0) {
								char tmp[7] = "0";
								strcat(tmp, buf2);
								SerialWrite(tmp);
							}
							else {
								char tmp[7] = "1";
								strcat(tmp, buf2);
								SerialWrite(tmp);
							}

#if debug == 1
							cout << dist << endl;
#endif
							char signal = SerialRead();// read 함수
							if (signal == 'f') {
								//물건앞에 도착했다는 신호 받으면
								while (1) {		//물건 주웠다 들어올때까지
									signal = SerialRead();
									if (signal = 'e') {		//
										buf_img.release();
										notFindingCount = 0;
										status = 0;
										char bufo[] = "o";
										SerialWrite(bufo);
									}
								}

								

							}
#endif
						}
						else if (centerX(rect) - size.width / 2 > 0) {	//물체 위치 우측
							char buf[] = "r";
							SerialWrite(buf);
#if debug == 1
							cout << "#Right" << endl;
#endif
						}
						else if (centerX(rect) - size.width / 2 < 0) {	//물체 위치 좌측
							char buf[] = "l";
							SerialWrite(buf);
#if debug == 1
							cout << "#Left" << endl;
#endif
						}
						else {
#if debug == 1
							cout << "error xxx" << endl;
#endif				
						}



					}


				}

			}
		}
	}


	//4분할
	//Mat resultImg, sui1, sui2;
	//cvtColor(cannyi, cannyi, COLOR_GRAY2BGR);
	//hconcat(bufImg, cannyi, sui1);
	//hconcat(drawing, inImg, sui2);
	//vconcat(sui1, sui2, resultImg);
	//imshow("result", resultImg);

	imshow("test22", plotImg);
}


double TemplateProcess(Rect rect, Mat resultImage, Mat tImage, double value) {
	/*인자로 받은 이미지에 대해 템플릿 매칭*/

	double minVal, maxVal;
	Point minLoc, maxLoc;
	Mat result;

	// 제곱차 매칭 방법(TM_SQDIFF_NORMED)
	matchTemplate(resultImage, tImage, result, TM_SQDIFF_NORMED);
	minMaxLoc(result, &minVal, NULL, &minLoc, NULL);

	Mat testImg = img_input.clone();
	if (minVal < value) {
#if debug == 1
		cout << "minVal:" << minVal << endl;
#endif
		return minVal;
	}

	return 1;

}


int centerX(Rect inRect) {
	return inRect.x + inRect.width / 2;
}

//동영상 저장
void videoWriteSet(Size size) {

	double fps = 30.0;
	writer.open("output.avi", VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, size, true);

	if (!writer.isOpened())
	{
		cout << "동영상을 저장하기 위한	초기화 작업 중 에러 발생" << endl;
		exit(0);
	}

}


/*시리얼 통신 기본셋팅*/
int SerialSet() {
#if SerialOn ==1	
	struct termios toptions;

	/* open serial port */
	fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY, 0666);
	//fd = open("/dev/ttyTSH2", O_RDWR | O_NOCTTY);
	printf("fd opened as %i\n", fd);

	/* wait for the Arduino to reboot */
	//usleep(1500000);

	/* get current serial port settings */
	tcgetattr(fd, &toptions);
	/* set 9600 baud both ways */
	cfsetispeed(&toptions, B9600);
	cfsetospeed(&toptions, B9600);
	/* 8 bits, no parity, no stop bits */
	toptions.c_cflag &= ~PARENB;
	toptions.c_cflag &= ~CSTOPB;
	toptions.c_cflag &= ~CSIZE;
	toptions.c_cflag &= CS8;
	/* Canonical mode */
	toptions.c_lflag |= ICANON;
	/* commit the serial port settings */
	tcsetattr(fd, TCSANOW, &toptions);
#endif
	return 0;
}

/*시리얼 통신 출력*/
int SerialWrite(char * buf)
{
	int len = strlen(buf);
	cout << "SerialOut: ";
	cout << buf;
	cout << "  len: " << len << endl;

#if SerialOn == 1
	/* Send byte to trigger Arduino to send string back */
	for (int i = 0; i < len; i++) {
		char str[2];
		sprintf(str, "%c", buf[i]);
		size_t nb;
		nb = strlen(str);
		write(fd, str, nb);
	}
	//sleep(1); //입력 딜레이
#endif 
	return 0;
}

char SerialRead() {
#if SerialOn == 1
	char buf[64];
	/* Receive string from Arduino */
	int n = read(fd, buf, 64);
	/* insert terminating zero in the string */
	   //buf[n] = 0;

	printf("%i bytes read, buffer contains: %s\n", n, buf);

	return buf[0];
#endif
	return 'f';
}

void addzero(char * strHex, int len) {
	while (strlen(strHex) < len) {
		char tmp[7] = "0";
		strcat(tmp, strHex);
		strcpy(strHex, tmp);
	}
}
