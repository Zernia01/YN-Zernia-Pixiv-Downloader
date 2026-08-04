# YN ZERNIA PIXIV Downloader 6.2

YN ZERNIA PIXIV 다운로더 6.2를 사용해 주셔서 감사합니다. 이 프로그램의 주요 기능과 사용법, 주의사항을 안내합니다.

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

* **종류 선택 및 동시 다운로드**
  * 메인 화면에서 `일러스트`, `만화`, `소설` 중 원하는 항목만 체크하면 선택한 종류만 다운로드합니다.
  * 동시 다운로드 수는 설정에서 **2~5개**로 지정할 수 있으며 기본값은 3개입니다.
  * 소설은 `작가 폴더/소설 제목 [ID]/`에 UTF-8 TXT 본문, 표지, 본문 삽화가 함께 저장됩니다.

* **자동 업데이트**
  * 설정의 `GitHub 저장소`에 `OWNER/REPOSITORY` 또는 저장소 URL을 입력하고 시작 시 자동 확인을 켤 수 있습니다.
  * GitHub의 최신 정식 Release 태그를 현재 버전과 비교하고, Release에 첨부된 `.exe`를 내려받습니다.
  * GitHub Release API가 제공하는 SHA-256 digest와 일치할 때만 설치하고 프로그램을 다시 시작합니다.

* **다국어 지원 (Languages)**
  * 다음 언어를 지원합니다: 한국어, 일본어, 중국어(간체/번체), 영어, 국한문혼용

---

## 🚀 2. 사용법 (Usage)

* **자동 픽시브 복사 (Auto Pixiv Copy)**
  * 이 기능을 **ON**으로 설정하면, 픽시브 작품 주소를 복사(`Ctrl + C`)하는 즉시 자동으로 다운로드가 시작됩니다.

* **중복 다운로드 방지 (Duplicate Download)**
  * 중복 다운로드 설정을 **OFF**로 지정하면, 로컬 아카이브 기록을 확인하여 이미 다운로드한 주소는 자동으로 스킵합니다.

* **프로필 주소 다운로드**
  * `https://www.pixiv.net/users/사용자ID`를 넣으면 체크한 종류만 가져옵니다.
  * `/illustrations`, `/manga`, `/novels` 주소도 지원하며, 해당 종류가 체크되어 있어야 다운로드합니다.

## 📦 3. GitHub Release 업데이트 배포

1. GitHub 저장소에서 새 Release를 만들고 태그를 `v6.3.0`처럼 현재 프로그램보다 높은 버전으로 지정합니다.
2. 빌드한 `YN Zernia Pixiv Downloader 6.2.exe` 파일을 Release asset으로 첨부합니다.
3. Draft와 Prerelease가 아닌 정식 Release로 게시합니다.
4. 사용자 설정의 `GitHub 저장소`에 `OWNER/REPOSITORY`를 입력합니다.

Release에 EXE가 여러 개 있으면 파일명에 `Zernia`와 `Pixiv`가 들어간 자산을 우선 선택합니다. 비공개 저장소는 인증 토큰이 필요하므로 현재 자동 업데이트는 공개 저장소용입니다.

---

## ⚠️ 4. 주의사항 (Notice)

다운로드가 정상적으로 진행되지 않거나 실패하는 경우, 아래의 원인일 수 있으니 확인해 주세요.
* 픽시브 서버의 일시적인 **과부하** 문제
* 삭제되었거나 접근할 수 없는 **오래된 페이지**를 다운로드하려고 시도한 경우

#EN

# YN ZERNIA PIXIV Downloader 6.2

Thank you for using YN ZERNIA PIXIV Downloader 6.2. Below is a guide to its main features, usage, and precautions.

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

* **Type Filters and Parallel Downloads**
  * Select `Illustrations`, `Manga`, and/or `Novels` on the main screen; only checked types are downloaded.
  * Configure **2–5** simultaneous downloads in Settings; the default is 3.
  * Each novel is saved in its own folder with a UTF-8 TXT file, cover, and embedded images.

* **Automatic Updates**
  * Enter `OWNER/REPOSITORY` or a GitHub repository URL in Settings and enable the startup update check.
  * The app compares the latest published GitHub Release tag and downloads its `.exe` asset.
  * It installs only when the file matches the SHA-256 digest returned by the GitHub Release API.

* **Supported Languages**
  * The application supports the following languages: Korean, Japanese, Chinese (Simplified/Traditional), English, and Korean Mixed Script (국한문혼용).

---

## 🚀 2. Usage

* **Auto Pixiv Copy**
  * If set to **ON**, simply copying a Pixiv artwork URL (`Ctrl + C`) will automatically trigger the download.

* **Duplicate Download Prevention**
  * If the duplicate download setting is turned **OFF**, the program will check your local archive history and automatically skip URLs that have already been downloaded.

* **Profile URL Downloads**
  * A `https://www.pixiv.net/users/USER_ID` URL expands only the selected content types.
  * `/illustrations`, `/manga`, and `/novels` profile URLs are also supported when their corresponding type is selected.

## 📦 3. Publishing Updates with GitHub Releases

1. Create a GitHub Release with a version tag newer than the app, such as `v6.3.0`.
2. Attach the built `YN Zernia Pixiv Downloader 6.2.exe` as a Release asset.
3. Publish it as a regular Release, not a draft or prerelease.
4. Enter `OWNER/REPOSITORY` in the app's GitHub Repository setting.

If a Release has multiple EXE assets, names containing `Zernia` and `Pixiv` are preferred. The current updater targets public repositories because private repositories require authentication.

---

## ⚠️ 4. Precautions / Notice

If a download fails or does not proceed, it may be due to one of the following reasons:
* Temporary **overload** on Pixiv's servers.
* Attempting to download an **outdated or deleted page** that is no longer accessible.
