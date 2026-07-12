#include <QImage>
#include <cstdio>
int main(){
    QImage img("D:/mviewer/selftest_main.png");
    if(img.isNull()){ printf("LOAD_FAIL\n"); return 1; }
    img = img.convertToFormat(QImage::Format_RGB32);
    int W=img.width(), H=img.height();
    printf("SIZE=%dx%d\n", W, H);
    auto lum=[&](int x,int y){ QRgb c=img.pixel(x,y); return (qRed(c)+qGreen(c)+qBlue(c))/3; };
    
    // Print per-column luminance profile (average over y, sampled)
    // Find transitions by detecting abrupt changes in column brightness
    int prev = -1;
    printf("COL_PROFILE ");
    for(int x=0; x<W; x+=20){
        long long s=0; int n=0;
        for(int y=0; y<H; y+=5){ s+=lum(x,y); n++; }
        int avg = (int)(s/n);
        printf("%d:%d ", x, avg);
    }
    printf("\n");
    
    // Count distinct "bands" by looking at column averages
    // Band transitions: find x where column average changes significantly
    int bands[10] = {0};
    int bandCount = 0;
    int lastAvg = -1;
    for(int x=0; x<W; x+=10){
        long long s=0; int n=0;
        for(int y=0; y<H; y+=5){ s+=lum(x,y); n++; }
        int avg = (int)(s/n);
        if(lastAvg==-1 || abs(avg-lastAvg)>20){
            if(bandCount<10) bands[bandCount++] = x;
            lastAvg = avg;
        }
    }
    printf("BANDS=%d POS=", bandCount);
    for(int i=0;i<bandCount;i++) printf("%d ", bands[i]);
    printf("\n");
    
    printf("DONE=1\n");
    return 0;
}
