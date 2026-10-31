#include "App.hpp"
#include "BfWebController.hpp"
#include "WebPages.hpp"
#include "WebTerms.hpp"

#include <filesystem>

using namespace drogon;
namespace fs = std::filesystem;

// This code was intended for generating placeholders in assets
//#define ENABLE_FILE_GENERATOR 1

#if ENABLE_FILE_GENERATOR
// Parse dimensions from filename (e.g., "MapVillage_640x0809.png" -> 640, 809)
static bool parseDimensions(const std::string& filename, int& width, int& height)
{
    size_t lastUnderscore = filename.find_last_of('_');
    size_t dotPos = filename.find_last_of('.');
    if (lastUnderscore == std::string::npos || dotPos == std::string::npos || lastUnderscore >= dotPos) {
        return false;
    }

    std::string dimStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
    size_t xPos = dimStr.find('x');
    if (xPos == std::string::npos) return false;

    try {
        width = std::stoi(dimStr.substr(0, xPos));
        height = std::stoi(dimStr.substr(xPos + 1));
        return true;
    }
    catch (...) {
        return false;
    }
}

// Generate a minimal PNG with specified dimensions
static std::vector<char> generatePlaceholderPng(int width, int height) {
    std::vector<char> pngData;

    // PNG signature
    const char signature[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    pngData.insert(pngData.end(), signature, signature + 8);

    // IHDR chunk
    uint32_t ihdrLength = htonl(13);
    pngData.insert(pngData.end(), (char*)&ihdrLength, (char*)&ihdrLength + 4);
    const char ihdrType[] = "IHDR";
    pngData.insert(pngData.end(), ihdrType, ihdrType + 4);

    uint32_t w = htonl(width);
    uint32_t h = htonl(height);
    pngData.insert(pngData.end(), (char*)&w, (char*)&w + 4);
    pngData.insert(pngData.end(), (char*)&h, (char*)&h + 4);

    const char ihdrData[] = { 8, 2, 0, 0, 0 }; // Bit depth 8, RGB, no compression, filter, interlace
    pngData.insert(pngData.end(), ihdrData, ihdrData + 5);

    uint32_t ihdrCrc = htonl(0); // Simplified CRC
    pngData.insert(pngData.end(), (char*)&ihdrCrc, (char*)&ihdrCrc + 4);

    // IDAT chunk (minimal data for a blank image)
    uint32_t idatLength = htonl(12 + width * 4 * height); // Adjust length based on dimensions
    pngData.insert(pngData.end(), (char*)&idatLength, (char*)&idatLength + 4);
    const char idatType[] = "IDAT";
    pngData.insert(pngData.end(), idatType, idatType + 4);

    // Minimal data: transparent pixels (RGBA)
    std::vector<char> idatData;
    for (int y = 0; y < height; ++y) {
        idatData.push_back(0); // Filter type
        for (int x = 0; x < width; ++x) {
            idatData.push_back(0); idatData.push_back(0); idatData.push_back(0); idatData.push_back(0); // Transparent
        }
    }
    pngData.insert(pngData.end(), idatData.begin(), idatData.end());

    uint32_t idatCrc = htonl(0);
    pngData.insert(pngData.end(), (char*)&idatCrc, (char*)&idatCrc + 4);

    // IEND chunk
    uint32_t iendLength = htonl(0);
    pngData.insert(pngData.end(), (char*)&iendLength, (char*)&iendLength + 4);
    const char iendType[] = "IEND";
    pngData.insert(pngData.end(), iendType, iendType + 4);
    uint32_t iendCrc = htonl(0);
    pngData.insert(pngData.end(), (char*)&iendCrc, (char*)&iendCrc + 4);

    return pngData;
}
#endif

void BfWebController::HandleWebPage(const HttpRequestPtr& rq, std::function<void(const HttpResponsePtr&)>&& callback)
{
    DumpLog log;
    theServer()->tryOpenHttpDumpLog("webpage", log);
    log << rq;

	// bfanew: generated pages for the in-app web view (WebPages.hpp); the client sends "//bf/web/..."
	{
		std::string rel = rq->getPath();
		while (rel.size() > 1 && rel[0] == '/' && rel[1] == '/') rel.erase(0, 1);
		if (rel == "/bf/web/rates/index.php")
		{
			drogon::async_run([rq, callback]() -> drogon::Task<> { co_await WebPages::Rates(rq, callback); });
			return;
		}
		if (rel == "/bf/web/notice/indexCtrl.php")
		{
			WebPages::Notice(rq, callback);
			return;
		}
	}

	if (rq->getPath() == "/bf/web/terms.htm")
	{
        // TODO: convert this to a drogon ctl controller
        auto resp = HttpResponse::newFileResponse((const unsigned char*)WEB_TERMS_DATA, strlen(WEB_TERMS_DATA), "", CT_TEXT_HTML);
        resp->setCloseConnection(true); // kill session
		callback(resp);
		return;
	}

    // Construct the full filesystem path
	auto path = getDocumentRoot() + rq->getPath();
    LOG_DEBUG << "Requesting path: " << path;

	// bfanew: deploy/overrides/<content|mst>/... (built by tools/build_overrides.py: patched text
	// tables, re-encoded MSTs) wins over the content archive, which is a read-only clone
	{
		std::string rel = rq->getPath();
		while (rel.size() > 1 && rel[0] == '/' && rel[1] == '/') rel.erase(0, 1);
		const auto over = fs::path("overrides") / fs::path(rel).relative_path();
		if ((rel.rfind("/content/", 0) == 0 || rel.rfind("/mst/", 0) == 0) && fs::is_regular_file(over))
		{
			LOG_DEBUG << "Served from overrides: " << rel;
			callback(HttpResponse::newFileResponse(over.string()));
			return;
		}
	}

    // Serve existing file if it exists
	if (!fs::exists(path) || fs::is_directory(path))
	{
		// bfanew: the content archive misses files the client's own bundle has (the village
		// animation's MapVillage_320x289.jpg / MapVillage_640x384.png: the archive carries an older
		// image set with the 2.19.6 .sam). Serving the blank image for those made the client cache a
		// 1x1 PNG and die with a heap corruption in the village. tools/fill_content_from_apk.py
		// extracts such APK assets into deploy/apk_assets/ (flat, by basename), served from here.
		const auto apkAsset = fs::path("apk_assets") / fs::path(rq->getPath()).filename();
		if (rq->getPath().rfind("/content/", 0) == 0 && fs::is_regular_file(apkAsset))
		{
			LOG_DEBUG << "Missing content served from the APK assets: " << rq->getPath();
			callback(HttpResponse::newFileResponse(apkAsset.string()));
			return;
		}

		logDlc() << rq->getPath() << "\n";

		// bfanew: the asset archive misses a few hundred files (mostly 2018-2022 summon
		// banners, see docs "Missing assets"). The client shows a blocking
		// "Error occurred during download" popup on any failed image, so answer
		// missing images with a valid 1x1 transparent PNG instead of a 404.
		// Other file types still get the 404 so that we notice them in dlc_404.log.
		const auto& rp = rq->getPath();
		const auto ext = fs::path(rp).extension().string();
		const auto base = fs::path(rp).filename().string();
		if (rp.rfind("/content/", 0) == 0 && (base == ".DS_Store" || base == "Thumbs.db"))
		{
			// the client's download list carries macOS junk entries (content/sam/5610BattleEffect
			// MeikoAdd/.DS_Store); a 404 stalls Settings > Downloadable Content > Full Download for
			// good, an empty file keeps it going
			LOG_DEBUG << "Junk list entry, serving an empty file: " << rp;
			auto resp = HttpResponse::newHttpResponse();
			resp->setStatusCode(k200OK);
			resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
			resp->setBody("");
			callback(resp);
			return;
		}
		if (rp.rfind("/content/", 0) == 0 && (ext == ".jpg" || ext == ".jpeg") && fs::is_regular_file("apk_assets/_blank.jpg"))
		{
			// the client picks the decoder from the extension, so a .jpg path needs JPEG bytes
			// (deploy/apk_assets/_blank.jpg, written by tools/fill_content_from_apk.py)
			LOG_DEBUG << "Missing asset, serving the blank jpg: " << rp;
			callback(HttpResponse::newFileResponse("apk_assets/_blank.jpg"));
			return;
		}
		if (rp.rfind("/content/", 0) == 0 && (ext == ".png" || ext == ".jpg" || ext == ".jpeg"))
		{
			static const unsigned char kBlankPng[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x7a, 0xa8, 0x57, 0x50, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			auto resp = HttpResponse::newFileResponse(kBlankPng, sizeof(kBlankPng), "", CT_IMAGE_PNG);
			LOG_DEBUG << "Missing asset, serving blank image: " << rp;
			callback(resp);
			return;
		}

		callback(HttpResponse::newNotFoundResponse());
	}
	else
	{
#if !ENABLE_FILE_GENERATOR
		callback(HttpResponse::newFileResponse(path));
#else
        LOG_INFO << "Generating placeholder for: " << path;

        try {
            // Ensure directory exists
            fs::path dirPath = path.substr(0, path.find_last_of('/'));
            if (!dirPath.empty() && !fs::exists(dirPath)) {
                fs::create_directories(dirPath);
            }

            std::ofstream outFile(path, std::ios::binary);
            if (!outFile.is_open()) {
                LOG_ERROR << "Failed to create placeholder: " << path;
                callback(HttpResponse::newNotFoundResponse());
                return;
            }

            // Generate placeholder based on file extension
            std::string filename = rq->getPath().substr(rq->getPath().find_last_of('/') + 1);
            if (filename.find(".png") != std::string::npos) {
                int width = 1, height = 1;
                parseDimensions(filename, width, height);
                auto placeholderPng = generatePlaceholderPng(width, height);
                outFile.write(placeholderPng.data(), placeholderPng.size());
            }
            else if (filename.find(".csv") != std::string::npos) {
                std::string placeholderCsv = "id,value\n0,placeholder";
                outFile.write(placeholderCsv.data(), placeholderCsv.size());
            }
            else if (filename.find(".dat") != std::string::npos) {
                std::string placeholderDat = "placeholder_game_data";
                outFile.write(placeholderDat.data(), placeholderDat.size());
            }
            else {
                std::string placeholderGeneric = "placeholder";
                outFile.write(placeholderGeneric.data(), placeholderGeneric.size());
            }
            outFile.close();
            LOG_INFO << "Generated placeholder for: " << path;

            // Serve the newly created placeholder file
            auto resp = HttpResponse::newFileResponse(path);
            // Drogon will automatically detect MIME type from extension
            callback(resp);
        }
        catch (const std::exception& e) {
            LOG_ERROR << "Error generating placeholder: " << e.what();
            callback(HttpResponse::newNotFoundResponse());
        }
#endif
	}
}

void BfWebController::HandleDefault(const HttpRequestPtr& rq, std::function<void(const HttpResponsePtr&)>&& callback)
{
	auto resp = HttpResponse::newHttpResponse();
	resp->setStatusCode(k200OK);
	resp->setBody("gimuserver server works!");
	callback(resp);
}
