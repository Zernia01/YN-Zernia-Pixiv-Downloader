# YN ZERNIA PIXIV Downloader 3.0

YN ZERNIA PIXIV 다운로더 3.0을 사용해 주셔서 감사합니다. 이 프로그램의 주요 기능과 사용법, 주의사항을 안내합니다.

## ⚙️ 1. 기능 (Features)

* 먼저 설정을 드가주세요!

* **토큰 (Token) 인증**
  * 인증창을 열어 안내에 따라 픽시브 토큰을 등록해 주세요.
  * ⚠️ **주의:** 한 번 토큰을 등록하면 `config` 파일에 저장됩니다. 개인정보 보호를 위해 토큰이 저장된 설정 파일은 다른 곳에 배포하지 않도록 주의하시기 바랍니다.

* **FFmpeg (움짤/동영상 저장 지원)**
  * 움짤(우고이라)을 정상적으로 저장하기 위해 필요한 구성 요소입니다.
  * 처음 사용 시 `winget`을 통해 자동으로 설치하거나, FFmpeg를 수동으로 다운로드하여 설치해 주세요.

* **프로필 페이지 설정 (Profile Page)**
  * 페이지 단위로 다운로드할 때, 1페이지당 몇 개의 작품을 처리할지 설정할 수 있습니다.
  * 안정적인 작동을 위해 **48**로 설정하는 것을 추천합니다.

* **다국어 지원 (Languages)**
  * 다음 언어를 지원합니다: 한국어, 일본어, 중국어(간체/번체), 영어, 국한문혼용

---

## 🚀 2. 사용법 (Usage)

* **자동 픽시브 복사 (Auto Pixiv Copy)**
  * 이 기능을 **ON**으로 설정하면, 픽시브 작품 주소를 복사(`Ctrl + C`)하는 즉시 자동으로 다운로드가 시작됩니다.

* **중복 다운로드 방지 (Duplicate Download)**
  * 중복 다운로드 설정을 **OFF**로 지정하면, 로컬 아카이브 기록을 확인하여 이미 다운로드한 주소는 자동으로 스킵합니다.

---

## ⚠️ 3. 주의사항 (Notice)

다운로드가 정상적으로 진행되지 않거나 실패하는 경우, 아래의 원인일 수 있으니 확인해 주세요.
* 픽시브 서버의 일시적인 **과부하** 문제
* 삭제되었거나 접근할 수 없는 **오래된 페이지**를 다운로드하려고 시도한 경우

#EN

# YN ZERNIA PIXIV Downloader 3.0

Thank you for using YN ZERNIA PIXIV Downloader 3.0. Below is a guide to its main features, usage, and precautions.

## ⚙️ 1. Features

* **Token Authentication**
  * Open the authentication window and follow the instructions to register your Pixiv token.
  * ⚠️ **Note:** Once registered, the token is saved in the `config` file. To protect your personal information, please do not share or distribute the configuration file to others.

* **FFmpeg (Animation/Ugoira Support)**
  * Required for properly saving animated images (Ugoira).
  * Upon first use, it can be installed automatically via `winget`, or you can download and install FFmpeg manually.

* **Profile Page Settings**
  * Sets the number of artworks to load per page when downloading by page.
  * We strongly recommend setting this to **48** for optimal stability.

* **Supported Languages**
  * The application supports the following languages: Korean, Japanese, Chinese (Simplified/Traditional), English, and Korean Mixed Script (국한문혼용).

---

## 🚀 2. Usage

* **Auto Pixiv Copy**
  * If set to **ON**, simply copying a Pixiv artwork URL (`Ctrl + C`) will automatically trigger the download.

* **Duplicate Download Prevention**
  * If the duplicate download setting is turned **OFF**, the program will check your local archive history and automatically skip URLs that have already been downloaded.

---

## ⚠️ 3. Precautions / Notice

If a download fails or does not proceed, it may be due to one of the following reasons:
* Temporary **overload** on Pixiv's servers.
* Attempting to download an **outdated or deleted page** that is no longer accessible.