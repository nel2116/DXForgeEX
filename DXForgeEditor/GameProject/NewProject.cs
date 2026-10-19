using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Xps;
using System.Xml.Linq;

namespace DXForgeEditor.GameProject
{

    [DataContract]
    public class ProjectTemplate
    {
        [DataMember]
        public string ProjectType { get; set; } = string.Empty;
        [DataMember]
        public string ProjectFile { get; set; } = string.Empty;
        [DataMember]
        public List<string> Folders { get; set; } = new();
        public byte[] Icon { get; set; } = Array.Empty<byte>();
        public byte[] Screenshot { get; set; } = Array.Empty<byte>();
        public string IconFilePath { get; set; } = string.Empty;
        public string ScreenshotFilePath { get; set; } = string.Empty;
        public string ProjectFilePath { get; set; } = string.Empty;
        public string TemplatePath { get; set; } = string.Empty;
    }

    class NewProject : ViewModelBase
    {
        // テンプレートはビルド時に出力フォルダへコピーされる(DXForgeEditor.csproj)。
        // 作業フォルダではなく実行ファイルの場所を基準にするので、どこから起動しても見つかる
        private static readonly string _templatePath = Path.Combine(AppContext.BaseDirectory, "ProjectTemplates");
        private string _projetName = "NewProject";

        public string ProjectName
        {
            get => _projetName;
            set
            {
                if (_projetName != value)
                {
                    _projetName = value;
                    ValidateProjectPath();
                    OnPropertyChanged(nameof(ProjectName));
                }
            }

        }
        private string _projectPath = $@"{Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)}\DXForgeProjects\";

        public string ProjectPath
        {
            get => _projectPath;
            set
            {
                if (_projectPath != value)
                {
                    _projectPath = value;
                    ValidateProjectPath();
                    OnPropertyChanged(nameof(ProjectPath));
                }
            }

        }

        private bool _isValid;
        public bool IsValid
        {
            get => _isValid;
            set
            {
                if (_isValid != value)
                {
                    _isValid = value;
                    OnPropertyChanged(nameof(IsValid));
                }
            }
        }

        private string _errorMsg = string.Empty;
        public string ErrorMsg
        {
            get => _errorMsg;
            set
            {
                if (_errorMsg != value)
                {
                    _errorMsg = value;
                    OnPropertyChanged(nameof(ErrorMsg));
                }
            }
        }

        private readonly ObservableCollection<ProjectTemplate> _projectTemplates = new();
        public ReadOnlyObservableCollection<ProjectTemplate> ProjectTemplates { get; }

        // Path欄の値の環境変数(%TEMP%など)を展開し、末尾を区切り文字にしたもの。
        // 絶対パスでなければnullを返す(相対パスは作業フォルダ次第で行き先が変わるため受け付けない)
        private string? GetExpandedProjectPath()
        {
            var path = Environment.ExpandEnvironmentVariables(ProjectPath.Trim());
            if (path.IndexOfAny(Path.GetInvalidPathChars()) != -1 || !Path.IsPathFullyQualified(path)) return null;
            if (!Path.EndsInDirectorySeparator(path)) path += @"\";
            return path;
        }

        private bool ValidateProjectPath()
        {
            var expandedPath = GetExpandedProjectPath();
            var path = $@"{expandedPath}{ProjectName}\";
            var nameRegex = new Regex(@"^[A-Za-z_][A-Za-z0-9_]*$");

            IsValid = false;
            if (string.IsNullOrWhiteSpace(ProjectName.Trim()))
            {
                ErrorMsg = "プロジェクト名を入力してください";
            }
            else if (!nameRegex.IsMatch(ProjectName))
            {
                ErrorMsg = "プロジェクト名に無効な文字が使用されています";
            }
            else if (string.IsNullOrWhiteSpace(ProjectPath.Trim()))
            {
                ErrorMsg = "有効なプロジェクトフォルダを選択してください";
            }
            else if (ProjectPath.IndexOfAny(Path.GetInvalidPathChars()) != -1)
            {
                ErrorMsg = "プロジェクトパスに無効な文字が使用されています";
            }
            else if (expandedPath == null)
            {
                ErrorMsg = "プロジェクトパスには絶対パスを入力してください";
            }
            else if (Directory.Exists(path) && Directory.EnumerateFileSystemEntries(path).Any())
            {
                ErrorMsg = "選択したプロジェクトフォルダはすでに存在しています";
            }
            else
            {
                ErrorMsg = string.Empty;
                IsValid = true;
            }

            return IsValid;
        }

        public string? CreateProject(ProjectTemplate template)
        {
            // プロジェクトパスが有効かどうか
            if (!ValidateProjectPath()) return null;

            var expandedPath = GetExpandedProjectPath();
            Debug.Assert(expandedPath != null);
            if (expandedPath == null) return null;
            // 実際に作る場所(展開済みの絶対パス)をPath欄にも表示する
            ProjectPath = expandedPath;
            var path = $@"{expandedPath}{ProjectName}\";

            try
            {
                if (!Directory.Exists(path)) Directory.CreateDirectory(path);
                foreach (var folder in template.Folders)
                {
                    Directory.CreateDirectory(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(path) ?? throw new InvalidOperationException("The project path has no parent directory."), folder)));
                }
                var dirinfo = new DirectoryInfo(path + @".DXForge\");
                dirinfo.Attributes |= FileAttributes.Hidden;
                File.Copy(template.IconFilePath, Path.GetFullPath(Path.Combine(dirinfo.FullName, "Icon.png")));
                File.Copy(template.ScreenshotFilePath, Path.GetFullPath(Path.Combine(dirinfo.FullName, "Screenshot.png")));

                var projectXml = File.ReadAllText(template.ProjectFilePath);
                projectXml = string.Format(projectXml, ProjectName, path);
                var projectPath = Path.GetFullPath(Path.Combine(path, $"{ProjectName}{Project.Extension}"));
                File.WriteAllText(projectPath, projectXml);

                CreateMSVCSolution(template, path);

                return path;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"作成に失敗: {ProjectName}");
                throw;
            }
        }

        private void CreateMSVCSolution(ProjectTemplate template, string projectPath)
        {
            Debug.Assert(File.Exists(Path.Combine(template.TemplatePath, "MSVCSolution")));
            Debug.Assert(File.Exists(Path.Combine(template.TemplatePath, "MSVCProject")));

            var engineAPIPath = @"$(DXFORGE_ENGINE)DXForgeEngine\EngineAPI\";
            Debug.Assert(Directory.Exists(engineAPIPath));

            var _0 = ProjectName;
            var _1 = "{" + Guid.NewGuid().ToString().ToUpper() + "}";
            var _2 = engineAPIPath;
            var _3 = "$(DXFORGE_ENGINE)";

            var solution = File.ReadAllText(Path.Combine(template.TemplatePath, "MSVCSolution"));
            solution = string.Format(solution, _0, _1, "{" + Guid.NewGuid().ToString().ToUpper() + "}");
            File.WriteAllText(Path.GetFullPath(Path.Combine(projectPath, $"{_0}.sln")), solution);

            var project = File.ReadAllText(Path.Combine(template.TemplatePath, "MSVCProject"));
            project = string.Format(project, _0, _1, _2, _3);
            File.WriteAllText(Path.GetFullPath(Path.Combine(projectPath, $@"GameCode\{_0}.vcxproj")), project);
        }

        public NewProject()
        {
            ProjectTemplates = new ReadOnlyObservableCollection<ProjectTemplate>(_projectTemplates);
            try
            {
                var templatesFiles = Directory.GetFiles(_templatePath, "template.xml", SearchOption.AllDirectories);
                Debug.Assert(templatesFiles.Any());
                foreach (var file in templatesFiles)
                {
                    var template = Serializer.FromFile<ProjectTemplate>(file);
                    template.TemplatePath = Path.GetDirectoryName(file) ?? throw new InvalidOperationException("The template path has no parent directory.");
                    template.IconFilePath = Path.GetFullPath(Path.Combine(template.TemplatePath, "Icon.png"));
                    template.Icon = File.ReadAllBytes(template.IconFilePath);
                    template.ScreenshotFilePath = Path.GetFullPath(Path.Combine(template.TemplatePath, "Screenshot.png"));
                    template.Screenshot = File.ReadAllBytes(template.ScreenshotFilePath);
                    template.ProjectFilePath = Path.GetFullPath(Path.Combine(template.TemplatePath, template.ProjectFile));

                    _projectTemplates.Add(template);
                }
                ValidateProjectPath();
            }
            catch (Exception ex)
            {
                // このコンストラクタはProjectBrowserDialogのXAMLから呼ばれるので、例外を投げ直すと
                // エディタが起動できなくなる。テンプレートの一覧を空にして、既存のプロジェクトは開けるようにする
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"プロジェクト・テンプレートの読み込みに失敗しました: {_templatePath}");
                _projectTemplates.Clear();
            }
        }
    }
}
