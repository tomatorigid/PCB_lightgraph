#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H

#include <QImage>

// Apply separable Gaussian blur to srcImage with given odd kernel size (3..7) and sigma
QImage applyGaussianBlur(const QImage& srcImage, int kernelSize, double sigma);

#endif // GAUSSIAN_BLUR_H

