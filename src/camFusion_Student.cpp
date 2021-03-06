
#include <iostream>
#include <algorithm>
#include <set>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Mat &topviewImg, bool bWait)
{
    // create topview image
    //cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));
    auto imageSize = topviewImg.size();
    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-(imageSize.width*0.15), bottom+(0.05*imageSize.height)), cv::FONT_ITALIC, 0.5, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-(imageSize.width*0.15), bottom+(0.10*imageSize.height)), cv::FONT_ITALIC, 0.5, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }
    
    // display image
    /*string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);
    
    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }*/
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
    for(auto match : kptMatches)
    {
        int prevIdx = match.queryIdx;
        int currIdx = match.trainIdx;
        auto pkp = kptsPrev[match.queryIdx];
        auto ckp = kptsCurr[match.trainIdx];
        
        if(boundingBox.roi.contains(ckp.pt))
        {
            boundingBox.kptMatches.push_back(match);
            boundingBox.keypoints.push_back(ckp);
        }
    }
}


// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
    // compute distance ratios between all matched keypoints
    vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (auto it1 = kptMatches.begin(); it1 != kptMatches.end() - 1; ++it1)
    { // outer kpt. loop

        // get current keypoint and its matched partner in the prev. frame
        cv::KeyPoint kpOuterCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(it1->queryIdx);

        for (auto it2 = kptMatches.begin() + 1; it2 != kptMatches.end(); ++it2)
        { // inner kpt.-loop

            double minDist = 100.0; // min. required distance

            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(it2->trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(it2->queryIdx);

            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);

            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            { // avoid division by zero

                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        } // eof inner loop over all matched kpts
    }     // eof outer loop over all matched kpts

    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }


    // STUDENT TASK (replacement for meanDistRatio)
    std::sort(distRatios.begin(), distRatios.end());
    long medIndex = floor(distRatios.size() / 2.0);
    double medDistRatio = distRatios.size() % 2 == 0 ? (distRatios[medIndex - 1] + distRatios[medIndex]) / 2.0 : distRatios[medIndex]; // compute median dist. ratio to remove outlier influence

    double dT = 1.0 / frameRate;
    TTC = -dT / (1 - medDistRatio);
    // EOF STUDENT TASK
}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    // auxiliary variables
    double dT = 1.0/frameRate; // time between two measurements in seconds
    
    // find closest distance to Lidar points 
    double minXPrev = 1e9, minXCurr = 1e9;
    const double y_slot_width = 0.10;
    set<double> minXs_Prev; //for each slot on Y, find closest point on X
    set<double> minXs_Curr; //for each slot on Y, find closest point on X
    
    std::map<int, double> minx_for_yslot;
    auto begin_it = begin(lidarPointsPrev);
    auto end_it = end(lidarPointsPrev);
    for(auto it=begin_it; it!=end_it; ++it)
    {
        int y_slot = it->y/y_slot_width;
        
        auto x_20cm_slot = minx_for_yslot.find(y_slot);
        if(x_20cm_slot == end(minx_for_yslot))
        {
            minx_for_yslot[y_slot] = it->x;
        }
        else
        {
            if(minx_for_yslot[y_slot] > it->x)
            {
                minx_for_yslot[y_slot] = it->x;
            }
        }
    }
    std::transform(begin(minx_for_yslot), end(minx_for_yslot),inserter(minXs_Prev,begin(minXs_Prev)), [](std::map<int,double>::value_type &pair){ return pair.second; });
    std::set<double>::iterator it = minXs_Prev.begin();
    std::advance(it, minXs_Prev.size()/3);
    minXPrev = *it;
    minx_for_yslot.clear();
    
    cout << "#LiDAR Distance correction ";
    for (auto i = minXs_Prev.begin(); i != minXs_Prev.end(); ++i)
        cout << *i << ", ";
    cout << "selected=" << minXPrev << " @=" << int(minXs_Prev.size()/3);
    cout << endl;
    
    begin_it = begin(lidarPointsCurr);
    end_it = end(lidarPointsCurr);
    for(auto it=begin_it; it!=end_it; ++it)
    {
        int y_slot = it->y/y_slot_width;
        
        auto x_20cm_slot = minx_for_yslot.find(y_slot);
        if(x_20cm_slot == end(minx_for_yslot))
        {
            minx_for_yslot[y_slot] = it->x;
        }
        else
        {
            if(minx_for_yslot[y_slot] > it->x)
            {
                minx_for_yslot[y_slot] = it->x;
            }
        }
    }
    
    std::transform(begin(minx_for_yslot), end(minx_for_yslot), inserter(minXs_Curr,begin(minXs_Curr)), [](std::map<int,double>::value_type &pair){ return pair.second; });
    std::set<double>::iterator it2 = minXs_Curr.begin();
    std::advance(it2, minXs_Curr.size()/3);
    minXCurr = *it2;
    minx_for_yslot.clear();
    
    // compute TTC from both measurements
    TTC = minXCurr * dT / (minXPrev-minXCurr);
}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    // ...
    //prevFrame->descriptors, currFrame->descriptors
    //BoundingBox in new frame, Bounding boxes in prev frame, number of matches
    std::map<pair<int,int>, int>  candidates;
    for(auto match : matches)
    {
        int currIdx = match.trainIdx;
        int prevIdx = match.queryIdx;
        auto pkp = prevFrame.keypoints[prevIdx];
        std::vector<int> prevFrameBoxes;
        for (auto pbBox : prevFrame.boundingBoxes)
        {
            if(pbBox.roi.contains(pkp.pt))
            {
                pbBox.kptMatches.push_back(match);
                pbBox.keypoints.push_back(pkp);
                prevFrameBoxes.push_back(pbBox.boxID);
            }
        }
        
        auto ckp = currFrame.keypoints[currIdx];
        for (auto cbBox : currFrame.boundingBoxes)
        {
            if(cbBox.roi.contains(ckp.pt))
            {
                cbBox.kptMatches.push_back(match);
                cbBox.keypoints.push_back(ckp);
                for(int pboxId : prevFrameBoxes)
                {
                    std::pair<int,int> candidate (pboxId,cbBox.boxID);
                    candidates[candidate] = candidates[candidate] + 1;
                }
            }
        }
    }
    for (auto cbBox : currFrame.boundingBoxes)
    {
        int bestMatchCount = 0;
        int bestMatch = 0;
        for (auto pbBox : prevFrame.boundingBoxes)
        {
            std::pair<int,int> candidate (pbBox.boxID,cbBox.boxID);
            if(candidates[candidate] > bestMatchCount)
            {
                bestMatchCount = candidates[candidate];
                bestMatch = pbBox.boxID;
            }
        }
        bbBestMatches[bestMatch] = cbBox.boxID;
    }
}
